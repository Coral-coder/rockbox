/***************************************************************************
 * Hosted (HiBy Linux) Bluetooth AVRCP target for the H2 / aigoerosq build.
 *
 * Lets a car head unit (CarPlay / Android Auto cars in their Bluetooth audio
 * mode, or any AVRCP controller such as headphones) control Rockbox and show
 * what is playing.
 *
 *  1. Remote keys. The stock BlueZ turns AVRCP pass-through commands
 *     (play, pause, next, previous, fast-forward, rewind, stop) into key
 *     events on a uinput device with bus type BUS_BLUETOOTH. We pick up such
 *     /dev/input/event* nodes, take them away from the generic button driver
 *     (whose key map is reversed for the H2's own buttons) and post Rockbox
 *     BUTTON_MULTIMEDIA_* events, handled globally in default_event_handler.
 *
 *  2. Now playing. We register a media player with bluetoothd over the
 *     system D-Bus so the car can show title / artist / album / position:
 *       - BlueZ 5: org.bluez.Media1.RegisterPlayer + MPRIS properties.
 *         BlueZ 5 then routes pass-through keys to our MPRIS methods.
 *       - BlueZ 4: org.bluez.Media.RegisterPlayer + org.bluez.MediaPlayer
 *         TrackChanged / PropertyChanged signals.
 *     libdbus-1 is loaded with dlopen() at runtime (the stock firmware ships
 *     it for dbus-daemon), so the build needs no D-Bus headers or libs and
 *     Rockbox still runs if it is missing (keys keep working).
 *
 * D-Bus runs on its own pthread with all signals blocked; it never calls
 * into Rockbox. The Rockbox-side worker thread takes snapshots of playback
 * state and executes commands the D-Bus thread queues.
 *
 * Diagnostics: /tmp/rb_bt_avrcp.log (and a copy on the SD card).
 ****************************************************************************/

#include "config.h"
#include "kernel.h"
#include "thread.h"
#include "button.h"
#include "audio.h"
#include "playlist.h"
#include "settings.h"
#include "metadata.h"
#include "cuesheet.h"
#include "bluetooth_avrcp_hosted.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#ifndef BUS_BLUETOOTH
#define BUS_BLUETOOTH 0x05
#endif

#define AVRCP_LOG_TMP    "/tmp/rb_bt_avrcp.log"
#define AVRCP_LOG_SD     "/mnt/sd_0/.rockbox/rb_bt_avrcp.log"
#define DBUS_SYSTEM_ADDR "unix:path=/var/run/dbus/system_bus_socket"
#define DBUS_SOCK_PATH   "/var/run/dbus/system_bus_socket"
#define PLAYER_PATH      "/org/rockbox/MediaPlayer"

#define MAX_INPUT_NODES  16  /* /dev/input/event0..15 */
#define BUTTON_NODES     5   /* button-devinput.c opens event0..4 at boot */
#define STR_MAX          128

/* from firmware/target/hosted/button-devinput.c */
void button_remove_input_device(int i);

/* ------------------------------------------------------------------ */
/* logging (safe from both threads: libc only)                         */
/* ------------------------------------------------------------------ */

static void avrcp_log(const char *fmt, ...)
{
    static const char *const paths[] = { AVRCP_LOG_TMP, AVRCP_LOG_SD };
    char msg[192];
    va_list ap;
    unsigned i;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *f = fopen(paths[i], "a");
        if (!f)
            continue;
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* shared state                                                        */
/* ------------------------------------------------------------------ */

enum avrcp_play_status {
    PS_STOPPED = 0,
    PS_PLAYING,
    PS_PAUSED,
};

struct avrcp_snapshot {
    int status;
    char title[STR_MAX];
    char artist[STR_MAX];
    char album[STR_MAX];
    char genre[64];
    uint32_t number;        /* 1-based track (or chapter) number */
    uint32_t count;         /* tracks in playlist (or chapters in book) */
    uint32_t duration_ms;
    uint32_t position_ms;
    unsigned track_gen;     /* bumps when metadata changes */
    unsigned status_gen;    /* bumps when play status changes */
    unsigned pos_gen;       /* bumps on seeks / unexpected position jumps */
};

/* commands queued by the D-Bus thread for the Rockbox thread */
#define CMD_PLAY     (1u << 0)
#define CMD_PAUSE    (1u << 1)
#define CMD_TOGGLE   (1u << 2)
#define CMD_STOP     (1u << 3)
#define CMD_NEXT     (1u << 4)
#define CMD_PREV     (1u << 5)
#define CMD_FFWD     (1u << 6)
#define CMD_REW      (1u << 7)

static pthread_mutex_t snap_lock = PTHREAD_MUTEX_INITIALIZER;
static struct avrcp_snapshot snap;      /* guarded by snap_lock */
static volatile unsigned pending_cmds;  /* __sync ops only */

static volatile bool dbus_run;
static pthread_t dbus_tid;
static bool dbus_tid_valid;

/* status reporting for the menu */
static volatile int keys_node_count;
static volatile int dbus_bluez_ver;     /* 0 none, 4 or 5 registered */
static volatile bool dbus_lib_missing;
static char status_text[48] = "Car controls: off";

static void queue_cmd(unsigned cmd)
{
    __sync_fetch_and_or(&pending_cmds, cmd);
}

/* ------------------------------------------------------------------ */
/* minimal libdbus-1 ABI, resolved with dlsym()                        */
/* ------------------------------------------------------------------ */

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef uint32_t dbus_bool_t;

typedef struct {
    const char *name;
    const char *message;
    unsigned int dummy;     /* dummy1..5 bitfields */
    void *padding1;
} DBusError;

/* libdbus' DBusMessageIter is 14 words on 32-bit; leave generous room. */
typedef union {
    void *align_p;
    uint64_t align_u;
    char pad[128];
} DBusMessageIter;

typedef enum {
    DBUS_HANDLER_RESULT_HANDLED,
    DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
    DBUS_HANDLER_RESULT_NEED_MEMORY,
} DBusHandlerResult;

typedef DBusHandlerResult (*dbus_msg_fn)(DBusConnection *, DBusMessage *,
                                         void *);
typedef struct {
    void (*unregister_function)(DBusConnection *, void *);
    dbus_msg_fn message_function;
    void (*pad1)(void *);
    void (*pad2)(void *);
    void (*pad3)(void *);
    void (*pad4)(void *);
} DBusObjectPathVTable;

#define DBUS_TYPE_INVALID     0
#define DBUS_TYPE_BOOLEAN     'b'
#define DBUS_TYPE_INT32       'i'
#define DBUS_TYPE_UINT32      'u'
#define DBUS_TYPE_INT64       'x'
#define DBUS_TYPE_DOUBLE      'd'
#define DBUS_TYPE_STRING      's'
#define DBUS_TYPE_OBJECT_PATH 'o'
#define DBUS_TYPE_ARRAY       'a'
#define DBUS_TYPE_VARIANT     'v'
#define DBUS_TYPE_DICT_ENTRY  'e'

#define DBUS_MESSAGE_TYPE_METHOD_CALL 1

static struct {
    void *lib;
    void (*error_init)(DBusError *);
    void (*error_free)(DBusError *);
    dbus_bool_t (*error_is_set)(const DBusError *);
    DBusConnection *(*connection_open_private)(const char *, DBusError *);
    dbus_bool_t (*bus_register)(DBusConnection *, DBusError *);
    void (*bus_add_match)(DBusConnection *, const char *, DBusError *);
    void (*connection_set_exit_on_disconnect)(DBusConnection *, dbus_bool_t);
    void (*connection_close)(DBusConnection *);
    void (*connection_unref)(DBusConnection *);
    dbus_bool_t (*connection_read_write_dispatch)(DBusConnection *, int);
    dbus_bool_t (*connection_register_object_path)(DBusConnection *,
                     const char *, const DBusObjectPathVTable *, void *);
    dbus_bool_t (*connection_add_filter)(DBusConnection *, dbus_msg_fn,
                                         void *, void (*)(void *));
    dbus_bool_t (*connection_send)(DBusConnection *, DBusMessage *,
                                   uint32_t *);
    void (*connection_flush)(DBusConnection *);
    DBusMessage *(*connection_send_with_reply_and_block)(DBusConnection *,
                     DBusMessage *, int, DBusError *);
    DBusMessage *(*message_new_method_call)(const char *, const char *,
                                            const char *, const char *);
    DBusMessage *(*message_new_signal)(const char *, const char *,
                                       const char *);
    DBusMessage *(*message_new_method_return)(DBusMessage *);
    void (*message_unref)(DBusMessage *);
    int (*message_get_type)(DBusMessage *);
    const char *(*message_get_interface)(DBusMessage *);
    const char *(*message_get_member)(DBusMessage *);
    dbus_bool_t (*message_is_signal)(DBusMessage *, const char *,
                                     const char *);
    void (*message_iter_init_append)(DBusMessage *, DBusMessageIter *);
    dbus_bool_t (*message_iter_append_basic)(DBusMessageIter *, int,
                                             const void *);
    dbus_bool_t (*message_iter_open_container)(DBusMessageIter *, int,
                                               const char *,
                                               DBusMessageIter *);
    dbus_bool_t (*message_iter_close_container)(DBusMessageIter *,
                                                DBusMessageIter *);
    dbus_bool_t (*message_iter_init)(DBusMessage *, DBusMessageIter *);
    int (*message_iter_get_arg_type)(DBusMessageIter *);
    void (*message_iter_get_basic)(DBusMessageIter *, void *);
} dl;

static bool dbus_lib_load(void)
{
    static const char *const names[] = {
        "libdbus-1.so.3", "/usr/lib/libdbus-1.so.3", "/lib/libdbus-1.so.3",
        "libdbus-1.so",
    };
    unsigned i;

    if (dl.lib)
        return true;

    for (i = 0; i < sizeof(names) / sizeof(names[0]) && !dl.lib; i++)
        dl.lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!dl.lib) {
        avrcp_log("dbus: libdbus-1 not found (%s)", dlerror());
        return false;
    }

#define SYM(field, name) \
    do { \
        *(void **)(&dl.field) = dlsym(dl.lib, name); \
        if (!dl.field) { \
            avrcp_log("dbus: missing symbol %s", name); \
            goto fail; \
        } \
    } while (0)

    SYM(error_init, "dbus_error_init");
    SYM(error_free, "dbus_error_free");
    SYM(error_is_set, "dbus_error_is_set");
    SYM(connection_open_private, "dbus_connection_open_private");
    SYM(bus_register, "dbus_bus_register");
    SYM(bus_add_match, "dbus_bus_add_match");
    SYM(connection_set_exit_on_disconnect,
        "dbus_connection_set_exit_on_disconnect");
    SYM(connection_close, "dbus_connection_close");
    SYM(connection_unref, "dbus_connection_unref");
    SYM(connection_read_write_dispatch, "dbus_connection_read_write_dispatch");
    SYM(connection_register_object_path,
        "dbus_connection_register_object_path");
    SYM(connection_add_filter, "dbus_connection_add_filter");
    SYM(connection_send, "dbus_connection_send");
    SYM(connection_flush, "dbus_connection_flush");
    SYM(connection_send_with_reply_and_block,
        "dbus_connection_send_with_reply_and_block");
    SYM(message_new_method_call, "dbus_message_new_method_call");
    SYM(message_new_signal, "dbus_message_new_signal");
    SYM(message_new_method_return, "dbus_message_new_method_return");
    SYM(message_unref, "dbus_message_unref");
    SYM(message_get_type, "dbus_message_get_type");
    SYM(message_get_interface, "dbus_message_get_interface");
    SYM(message_get_member, "dbus_message_get_member");
    SYM(message_is_signal, "dbus_message_is_signal");
    SYM(message_iter_init_append, "dbus_message_iter_init_append");
    SYM(message_iter_append_basic, "dbus_message_iter_append_basic");
    SYM(message_iter_open_container, "dbus_message_iter_open_container");
    SYM(message_iter_close_container, "dbus_message_iter_close_container");
    SYM(message_iter_init, "dbus_message_iter_init");
    SYM(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
    SYM(message_iter_get_basic, "dbus_message_iter_get_basic");
#undef SYM
    return true;

fail:
    dlclose(dl.lib);
    memset(&dl, 0, sizeof(dl));
    return false;
}

/* ------------------------------------------------------------------ */
/* D-Bus message helpers                                               */
/* ------------------------------------------------------------------ */

/* libdbus aborts the process on invalid UTF-8, so sanitise everything. */
static void utf8_sanitize(char *dst, const char *src, size_t dst_len)
{
    const unsigned char *s = (const unsigned char *)(src ? src : "");
    size_t o = 0;

    while (*s && o + 1 < dst_len) {
        unsigned char c = *s;
        int need = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 :
                   (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
        int k;

        if (need < 0 || (need == 1 && c < 0xc2)) {
            dst[o++] = '?';
            s++;
            continue;
        }
        for (k = 1; k <= need; k++)
            if ((s[k] & 0xc0) != 0x80)
                break;
        if (k <= need) {
            dst[o++] = '?';
            s++;
            continue;
        }
        if (o + need + 1 >= dst_len)
            break;
        for (k = 0; k <= need; k++)
            dst[o++] = (char)s[k];
        s += need + 1;
    }
    dst[o] = '\0';
}

static void it_basic(DBusMessageIter *it, int type, const void *val)
{
    dl.message_iter_append_basic(it, type, val);
}

static void it_str(DBusMessageIter *it, const char *s)
{
    it_basic(it, DBUS_TYPE_STRING, &s);
}

/* dict entry {key: variant(type sig, value)} inside an a{sv} */
static void dict_basic(DBusMessageIter *dict, const char *key, int type,
                       const char *sig, const void *val)
{
    DBusMessageIter e, v;

    dl.message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    it_str(&e, key);
    dl.message_iter_open_container(&e, DBUS_TYPE_VARIANT, sig, &v);
    it_basic(&v, type, val);
    dl.message_iter_close_container(&e, &v);
    dl.message_iter_close_container(dict, &e);
}

static void dict_str(DBusMessageIter *d, const char *key, const char *val)
{
    dict_basic(d, key, DBUS_TYPE_STRING, "s", &val);
}

static void dict_u32(DBusMessageIter *d, const char *key, uint32_t val)
{
    dict_basic(d, key, DBUS_TYPE_UINT32, "u", &val);
}

static void dict_i32(DBusMessageIter *d, const char *key, int32_t val)
{
    dict_basic(d, key, DBUS_TYPE_INT32, "i", &val);
}

static void dict_i64(DBusMessageIter *d, const char *key, int64_t val)
{
    dict_basic(d, key, DBUS_TYPE_INT64, "x", &val);
}

static void dict_bool(DBusMessageIter *d, const char *key, bool b)
{
    dbus_bool_t val = b ? 1 : 0;
    dict_basic(d, key, DBUS_TYPE_BOOLEAN, "b", &val);
}

static void dict_obj(DBusMessageIter *d, const char *key, const char *path)
{
    dict_basic(d, key, DBUS_TYPE_OBJECT_PATH, "o", &path);
}

/* {key: variant(as[val])} */
static void dict_strarray(DBusMessageIter *dict, const char *key,
                          const char *val)
{
    DBusMessageIter e, v, a;

    dl.message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    it_str(&e, key);
    dl.message_iter_open_container(&e, DBUS_TYPE_VARIANT, "as", &v);
    dl.message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &a);
    it_str(&a, val);
    dl.message_iter_close_container(&v, &a);
    dl.message_iter_close_container(&e, &v);
    dl.message_iter_close_container(dict, &e);
}

static void open_dict(DBusMessageIter *parent, DBusMessageIter *dict)
{
    dl.message_iter_open_container(parent, DBUS_TYPE_ARRAY, "{sv}", dict);
}

static void close_dict(DBusMessageIter *parent, DBusMessageIter *dict)
{
    dl.message_iter_close_container(parent, dict);
}

/* ------------------------------------------------------------------ */
/* BlueZ player registration and updates (D-Bus thread only)           */
/* ------------------------------------------------------------------ */

static DBusConnection *conn;
static int bluez_ver;                /* 0 = not registered, 4 or 5 */
static char bluez_adapter[64];
static bool bluez_owner_changed;     /* bluetoothd restarted */
static struct avrcp_snapshot sent;   /* last state we reported */

static const char *mpris_status(int st)
{
    return st == PS_PLAYING ? "Playing" : st == PS_PAUSED ? "Paused"
                                                          : "Stopped";
}

static const char *bz4_status(int st)
{
    return st == PS_PLAYING ? "playing" : st == PS_PAUSED ? "paused"
                                                          : "stopped";
}

static const char *snap_title(const struct avrcp_snapshot *s)
{
    return s->title[0] ? s->title : "Rockbox";
}

/* MPRIS Metadata a{sv} (BlueZ 5) */
static void bz5_append_metadata(DBusMessageIter *dict,
                                const struct avrcp_snapshot *s)
{
    DBusMessageIter e, v, md;
    char trackid[48];

    snprintf(trackid, sizeof(trackid), "/org/rockbox/track/%u",
             s->track_gen);

    dl.message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    it_str(&e, "Metadata");
    dl.message_iter_open_container(&e, DBUS_TYPE_VARIANT, "a{sv}", &v);
    open_dict(&v, &md);
    dict_obj(&md, "mpris:trackid", trackid);
    dict_str(&md, "xesam:title", snap_title(s));
    if (s->artist[0])
        dict_strarray(&md, "xesam:artist", s->artist);
    if (s->album[0])
        dict_str(&md, "xesam:album", s->album);
    if (s->genre[0])
        dict_strarray(&md, "xesam:genre", s->genre);
    if (s->number)
        dict_i32(&md, "xesam:trackNumber", (int32_t)s->number);
    dict_i64(&md, "mpris:length", (int64_t)s->duration_ms * 1000);
    close_dict(&v, &md);
    dl.message_iter_close_container(&e, &v);
    dl.message_iter_close_container(dict, &e);
}

/* BlueZ 4 metadata a{sv} */
static void bz4_append_metadata(DBusMessageIter *parent,
                                const struct avrcp_snapshot *s)
{
    DBusMessageIter md;

    open_dict(parent, &md);
    dict_str(&md, "Title", snap_title(s));
    dict_str(&md, "Artist", s->artist);
    dict_str(&md, "Album", s->album);
    dict_str(&md, "Genre", s->genre);
    dict_u32(&md, "NumberOfTracks", s->count);
    dict_u32(&md, "Number", s->number);
    dict_u32(&md, "Duration", s->duration_ms);
    close_dict(parent, &md);
}

/* Registration is retried every 10 s while bluetoothd is absent or busy;
 * only log the first round of failures so the SD log stays small. */
static bool log_reg_errors = true;

static DBusMessage *call_block(DBusMessage *msg, const char *what)
{
    DBusError err;
    DBusMessage *reply;

    dl.error_init(&err);
    reply = dl.connection_send_with_reply_and_block(conn, msg, 3000, &err);
    dl.message_unref(msg);
    if (!reply) {
        if (log_reg_errors)
            avrcp_log("dbus: %s failed: %s", what,
                      dl.error_is_set(&err) && err.message ? err.message
                                                           : "?");
        dl.error_free(&err);
    }
    return reply;
}

static bool bz5_register(const struct avrcp_snapshot *s)
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, dict;
    const char *path = PLAYER_PATH;

    msg = dl.message_new_method_call("org.bluez", "/org/bluez/hci0",
                                     "org.bluez.Media1", "RegisterPlayer");
    if (!msg)
        return false;
    dl.message_iter_init_append(msg, &it);
    it_basic(&it, DBUS_TYPE_OBJECT_PATH, &path);
    open_dict(&it, &dict);
    dict_str(&dict, "PlaybackStatus", mpris_status(s->status));
    dict_str(&dict, "LoopStatus", "None");
    dict_bool(&dict, "Shuffle", false);
    dict_i64(&dict, "Position", (int64_t)s->position_ms * 1000);
    dict_bool(&dict, "CanGoNext", true);
    dict_bool(&dict, "CanGoPrevious", true);
    dict_bool(&dict, "CanPlay", true);
    dict_bool(&dict, "CanPause", true);
    dict_bool(&dict, "CanSeek", true);
    dict_bool(&dict, "CanControl", true);
    dict_str(&dict, "Identity", "Rockbox");
    bz5_append_metadata(&dict, s);
    close_dict(&it, &dict);

    reply = call_block(msg, "BlueZ5 RegisterPlayer");
    if (!reply)
        return false;
    dl.message_unref(reply);
    return true;
}

static bool bz4_find_adapter(void)
{
    DBusMessage *msg, *reply;
    DBusMessageIter it;
    const char *path = NULL;

    msg = dl.message_new_method_call("org.bluez", "/", "org.bluez.Manager",
                                     "DefaultAdapter");
    if (!msg)
        return false;
    reply = call_block(msg, "BlueZ4 DefaultAdapter");
    if (!reply)
        return false;
    if (dl.message_iter_init(reply, &it)
        && dl.message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH)
        dl.message_iter_get_basic(&it, &path);
    if (path)
        snprintf(bluez_adapter, sizeof(bluez_adapter), "%s", path);
    dl.message_unref(reply);
    return path != NULL;
}

static bool bz4_register(const struct avrcp_snapshot *s)
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, dict;
    const char *path = PLAYER_PATH;

    if (!bz4_find_adapter())
        return false;

    msg = dl.message_new_method_call("org.bluez", bluez_adapter,
                                     "org.bluez.Media", "RegisterPlayer");
    if (!msg)
        return false;
    dl.message_iter_init_append(msg, &it);
    it_basic(&it, DBUS_TYPE_OBJECT_PATH, &path);
    open_dict(&it, &dict);
    dict_str(&dict, "Equalizer", "off");
    dict_str(&dict, "Repeat", "off");
    dict_str(&dict, "Shuffle", "off");
    dict_str(&dict, "Scan", "off");
    dict_str(&dict, "Status", bz4_status(s->status));
    dict_u32(&dict, "Position", s->position_ms);
    close_dict(&it, &dict);
    bz4_append_metadata(&it, s);

    reply = call_block(msg, "BlueZ4 RegisterPlayer");
    if (!reply)
        return false;
    dl.message_unref(reply);
    return true;
}

static void send_msg(DBusMessage *msg)
{
    if (!msg)
        return;
    dl.connection_send(conn, msg, NULL);
    dl.message_unref(msg);
}

/* BlueZ 5: org.freedesktop.DBus.Properties.PropertiesChanged */
static void bz5_props_changed(const struct avrcp_snapshot *s, bool meta,
                              bool status, bool pos)
{
    DBusMessage *msg;
    DBusMessageIter it, dict, inval;

    msg = dl.message_new_signal(PLAYER_PATH,
                                "org.freedesktop.DBus.Properties",
                                "PropertiesChanged");
    if (!msg)
        return;
    dl.message_iter_init_append(msg, &it);
    it_str(&it, "org.mpris.MediaPlayer2.Player");
    open_dict(&it, &dict);
    if (meta)
        bz5_append_metadata(&dict, s);
    if (status)
        dict_str(&dict, "PlaybackStatus", mpris_status(s->status));
    if (pos)
        dict_i64(&dict, "Position", (int64_t)s->position_ms * 1000);
    close_dict(&it, &dict);
    dl.message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dl.message_iter_close_container(&it, &inval);
    send_msg(msg);
}

/* BlueZ 4: org.bluez.MediaPlayer.PropertyChanged(s, v) */
static void bz4_prop_changed(const char *name, int type, const char *sig,
                             const void *val)
{
    DBusMessage *msg;
    DBusMessageIter it, v;

    msg = dl.message_new_signal(PLAYER_PATH, "org.bluez.MediaPlayer",
                                "PropertyChanged");
    if (!msg)
        return;
    dl.message_iter_init_append(msg, &it);
    it_str(&it, name);
    dl.message_iter_open_container(&it, DBUS_TYPE_VARIANT, sig, &v);
    it_basic(&v, type, val);
    dl.message_iter_close_container(&it, &v);
    send_msg(msg);
}

static void bz4_track_changed(const struct avrcp_snapshot *s)
{
    DBusMessage *msg;
    DBusMessageIter it;

    msg = dl.message_new_signal(PLAYER_PATH, "org.bluez.MediaPlayer",
                                "TrackChanged");
    if (!msg)
        return;
    dl.message_iter_init_append(msg, &it);
    bz4_append_metadata(&it, s);
    send_msg(msg);
}

static void publish_changes(void)
{
    struct avrcp_snapshot s;
    bool meta, status, pos;

    pthread_mutex_lock(&snap_lock);
    s = snap;
    pthread_mutex_unlock(&snap_lock);

    meta = s.track_gen != sent.track_gen;
    status = s.status_gen != sent.status_gen;
    pos = meta || status || s.pos_gen != sent.pos_gen;
    if (!meta && !status && !pos)
        return;

    if (bluez_ver == 5) {
        bz5_props_changed(&s, meta, status, pos);
    } else if (bluez_ver == 4) {
        if (meta)
            bz4_track_changed(&s);
        if (status) {
            const char *st = bz4_status(s.status);
            bz4_prop_changed("Status", DBUS_TYPE_STRING, "s", &st);
        }
        if (pos) {
            uint32_t p = s.position_ms;
            bz4_prop_changed("Position", DBUS_TYPE_UINT32, "u", &p);
        }
    }
    dl.connection_flush(conn);
    sent = s;
}

/* Method calls from bluetoothd on our player object. */
static DBusHandlerResult player_message(DBusConnection *c, DBusMessage *msg,
                                        void *data)
{
    const char *iface, *member;
    DBusMessage *reply;
    unsigned cmd = 0;
    bool known = true;

    (void)data;
    if (dl.message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    iface = dl.message_get_interface(msg);
    member = dl.message_get_member(msg);
    if (!member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!strcmp(member, "Play"))
        cmd = CMD_PLAY;
    else if (!strcmp(member, "Pause"))
        cmd = CMD_PAUSE;
    else if (!strcmp(member, "PlayPause"))
        cmd = CMD_TOGGLE;
    else if (!strcmp(member, "Stop"))
        cmd = CMD_STOP;
    else if (!strcmp(member, "Next"))
        cmd = CMD_NEXT;
    else if (!strcmp(member, "Previous"))
        cmd = CMD_PREV;
    else if (!strcmp(member, "FastForward"))
        cmd = CMD_FFWD;
    else if (!strcmp(member, "Rewind"))
        cmd = CMD_REW;
    else if (!strcmp(member, "Seek")) {
        DBusMessageIter it;
        int64_t off = 0;
        if (dl.message_iter_init(msg, &it)
            && dl.message_iter_get_arg_type(&it) == DBUS_TYPE_INT64)
            dl.message_iter_get_basic(&it, &off);
        cmd = off < 0 ? CMD_REW : CMD_FFWD;
    }
    else if (!strcmp(member, "Release")) {
        avrcp_log("dbus: player released by bluetoothd");
        bluez_ver = 0;
        dbus_bluez_ver = 0;
    }
    else if (!strcmp(member, "SetProperty") || !strcmp(member, "Set")
             || !strcmp(member, "SetPosition")) {
        /* repeat/shuffle/volume requests: accept and ignore */
    }
    else
        known = false;

    if (!known)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (cmd) {
        avrcp_log("dbus: %s.%s", iface ? iface : "?", member);
        queue_cmd(cmd);
    }

    reply = dl.message_new_method_return(msg);
    if (reply) {
        dl.connection_send(c, reply, NULL);
        dl.message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

/* Watch for bluetoothd restarts so we can register again. */
static DBusHandlerResult bus_filter(DBusConnection *c, DBusMessage *msg,
                                    void *data)
{
    (void)c;
    (void)data;
    if (dl.message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        DBusMessageIter it;
        const char *name = NULL;
        if (dl.message_iter_init(msg, &it)
            && dl.message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
            dl.message_iter_get_basic(&it, &name);
            if (name && !strcmp(name, "org.bluez"))
                bluez_owner_changed = true;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void dbus_disconnect(void)
{
    if (conn) {
        dl.connection_close(conn);
        dl.connection_unref(conn);
        conn = NULL;
    }
    bluez_ver = 0;
    dbus_bluez_ver = 0;
}

static bool dbus_connect(void)
{
    static const DBusObjectPathVTable vtable = {
        .unregister_function = NULL,
        .message_function = player_message,
    };
    DBusError err;

    if (access(DBUS_SOCK_PATH, F_OK) != 0)
        return false;

    dl.error_init(&err);
    conn = dl.connection_open_private(DBUS_SYSTEM_ADDR, &err);
    if (!conn) {
        dl.error_free(&err);
        return false;
    }
    /* Never let libdbus _exit() Rockbox when the bus goes away. */
    dl.connection_set_exit_on_disconnect(conn, 0);
    if (!dl.bus_register(conn, &err)) {
        avrcp_log("dbus: bus register failed: %s",
                  err.message ? err.message : "?");
        dl.error_free(&err);
        dbus_disconnect();
        return false;
    }
    if (!dl.connection_register_object_path(conn, PLAYER_PATH, &vtable,
                                            NULL)) {
        avrcp_log("dbus: object path register failed");
        dbus_disconnect();
        return false;
    }
    dl.connection_add_filter(conn, bus_filter, NULL, NULL);
    dl.bus_add_match(conn,
                     "type='signal',sender='org.freedesktop.DBus',"
                     "member='NameOwnerChanged',arg0='org.bluez'", &err);
    if (dl.error_is_set(&err))
        dl.error_free(&err);
    avrcp_log("dbus: connected to system bus");
    return true;
}

static void try_register_player(void)
{
    struct avrcp_snapshot s;

    pthread_mutex_lock(&snap_lock);
    s = snap;
    pthread_mutex_unlock(&snap_lock);

    if (bz5_register(&s))
        bluez_ver = 5;
    else if (bz4_register(&s))
        bluez_ver = 4;

    if (bluez_ver) {
        sent = s;
        avrcp_log("dbus: media player registered (BlueZ %d)", bluez_ver);
        log_reg_errors = true;
    } else {
        if (log_reg_errors)
            avrcp_log("dbus: no BlueZ media API yet, retrying quietly");
        log_reg_errors = false;
    }
    dbus_bluez_ver = bluez_ver;
}

static void *dbus_thread_fn(void *arg)
{
    unsigned retry_ms = 0;

    (void)arg;
    if (!dbus_lib_load()) {
        dbus_lib_missing = true;
        return NULL;
    }

    while (dbus_run) {
        if (!conn) {
            if (retry_ms) {
                usleep(250 * 1000);
                retry_ms = retry_ms > 250 ? retry_ms - 250 : 0;
                continue;
            }
            if (!dbus_connect()) {
                retry_ms = 5000;
                continue;
            }
        }

        if (!bluez_ver || bluez_owner_changed) {
            if (bluez_owner_changed) {
                avrcp_log("dbus: bluetoothd changed, re-registering");
                bluez_owner_changed = false;
                bluez_ver = 0;
                dbus_bluez_ver = 0;
                /* give bluetoothd a moment to export its objects */
                retry_ms = 1000;
            }
            if (retry_ms == 0) {
                try_register_player();
                if (!bluez_ver)
                    retry_ms = 10000;
            }
        }

        if (!dl.connection_read_write_dispatch(conn, 250)) {
            avrcp_log("dbus: disconnected");
            dbus_disconnect();
            retry_ms = 2000;
            continue;
        }
        if (retry_ms)
            retry_ms = retry_ms > 250 ? retry_ms - 250 : 0;

        if (bluez_ver)
            publish_changes();
    }

    dbus_disconnect();
    return NULL;
}

static void dbus_thread_start(void)
{
    sigset_t all, old;

    if (dbus_tid_valid)
        return;
    dbus_run = true;
    /* Rockbox's tick and thread switching are signal driven; keep every
     * signal on the main thread. The new thread inherits this mask. */
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    if (pthread_create(&dbus_tid, NULL, dbus_thread_fn, NULL) == 0)
        dbus_tid_valid = true;
    else
        dbus_run = false;
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}

static void dbus_thread_stop(void)
{
    if (!dbus_tid_valid)
        return;
    dbus_run = false;
    pthread_join(dbus_tid, NULL);
    dbus_tid_valid = false;
}

/* ------------------------------------------------------------------ */
/* Rockbox side: playback snapshots                                    */
/* ------------------------------------------------------------------ */

static void take_snapshot(void)
{
    static char last_key[MAX_PATH + 16];
    static int last_status = -1;
    static uint32_t last_pos;
    static long last_tick;
    struct avrcp_snapshot s;
    struct mp3entry *id3 = NULL;
    char key[MAX_PATH + 16];
    int st = audio_status();
    long now = current_tick;

    memset(&s, 0, sizeof(s));
    s.status = !(st & AUDIO_STATUS_PLAY) ? PS_STOPPED :
               (st & AUDIO_STATUS_PAUSE) ? PS_PAUSED : PS_PLAYING;

    if (st & AUDIO_STATUS_PLAY)
        id3 = audio_current_track();

    key[0] = '\0';
    if (id3) {
        struct cuesheet *cue = id3->cuesheet;
        const char *title = id3->title;
        const char *artist = id3->artist ? id3->artist : id3->albumartist;
        uint32_t start = 0, end = id3->length;

        s.number = playlist_get_display_index();
        s.count = playlist_amount();

        if (cue && cue->curr_track && cue->track_count > 0) {
            /* Chapter / cue track: report it as the "track" to the car. */
            int idx = cue_find_current_track(cue, id3->elapsed);
            struct cue_track_info *t = &cue->tracks[idx];

            if (t->title[0])
                title = t->title;
            if (t->performer[0])
                artist = t->performer;
            start = t->offset;
            if (idx + 1 < cue->track_count)
                end = cue->tracks[idx + 1].offset;
            s.number = idx + 1;
            s.count = cue->track_count;
            utf8_sanitize(s.album, cue->title[0] ? cue->title :
                          (id3->album ? id3->album : id3->title), STR_MAX);
        } else {
            utf8_sanitize(s.album, id3->album, STR_MAX);
        }

        if (!title || !*title) {
            const char *slash = strrchr(id3->path, '/');
            title = slash ? slash + 1 : id3->path;
        }
        utf8_sanitize(s.title, title, STR_MAX);
        utf8_sanitize(s.artist, artist, STR_MAX);
        utf8_sanitize(s.genre, id3->genre_string, sizeof(s.genre));

        s.duration_ms = end > start ? end - start : 0;
        s.position_ms = id3->elapsed > start ? id3->elapsed - start : 0;
        if (s.position_ms > s.duration_ms)
            s.position_ms = s.duration_ms;

        snprintf(key, sizeof(key), "%s#%lu", id3->path,
                 (unsigned long)start);
    }

    pthread_mutex_lock(&snap_lock);
    s.track_gen = snap.track_gen;
    s.status_gen = snap.status_gen;
    s.pos_gen = snap.pos_gen;

    if (strcmp(key, last_key) || strcmp(s.title, snap.title)
        || strcmp(s.artist, snap.artist) || strcmp(s.album, snap.album)
        || s.duration_ms != snap.duration_ms) {
        s.track_gen++;
        strcpy(last_key, key);
    }
    if (s.status != last_status) {
        s.status_gen++;
        last_status = s.status;
    } else {
        /* Seek detection: compare against where playback should be. */
        long expect = last_pos;
        long diff;
        if (s.status == PS_PLAYING)
            expect += (now - last_tick) * 1000 / HZ;
        diff = (long)s.position_ms - expect;
        if (diff > 2000 || diff < -2000)
            s.pos_gen++;
    }
    snap = s;
    pthread_mutex_unlock(&snap_lock);

    last_pos = s.position_ms;
    last_tick = now;
}

/* Execute a car command. Explicit play/pause must not blindly toggle. */
static void run_cmd(unsigned cmd)
{
    int st = audio_status();
    bool playing = (st & AUDIO_STATUS_PLAY) && !(st & AUDIO_STATUS_PAUSE);

    if (cmd & CMD_TOGGLE)
        button_queue_post(BUTTON_MULTIMEDIA_PLAYPAUSE, 0);
    if ((cmd & CMD_PLAY) && !playing)
        button_queue_post(BUTTON_MULTIMEDIA_PLAYPAUSE, 0);
    /* Cars send Stop when switching source: pause so the place is kept. */
    if ((cmd & (CMD_PAUSE | CMD_STOP)) && playing)
        button_queue_post(BUTTON_MULTIMEDIA_PLAYPAUSE, 0);
    if (cmd & CMD_NEXT)
        button_queue_post(BUTTON_MULTIMEDIA_NEXT, 0);
    if (cmd & CMD_PREV)
        button_queue_post(BUTTON_MULTIMEDIA_PREV, 0);
    if (cmd & CMD_FFWD)
        button_queue_post(BUTTON_MULTIMEDIA_FFWD, 0);
    if (cmd & CMD_REW)
        button_queue_post(BUTTON_MULTIMEDIA_REW, 0);
}

/* ------------------------------------------------------------------ */
/* Rockbox side: AVRCP uinput key devices                              */
/* ------------------------------------------------------------------ */

static int input_fd[MAX_INPUT_NODES];
static bool input_checked[MAX_INPUT_NODES]; /* non-BT node, skip */
static long last_seek_key_tick;

static void input_close(int i)
{
    if (input_fd[i] >= 0) {
        close(input_fd[i]);
        input_fd[i] = -1;
        keys_node_count--;
        avrcp_log("keys: event%d closed", i);
    }
}

static void input_scan(void)
{
    int i;

    for (i = 0; i < MAX_INPUT_NODES; i++) {
        char path[32];
        char name[64];
        struct input_id id;
        int fd;

        if (input_fd[i] >= 0 || input_checked[i])
            continue;

        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        memset(&id, 0, sizeof(id));
        if (ioctl(fd, EVIOCGID, &id) < 0 || id.bustype != BUS_BLUETOOTH) {
            /* Built-in keys / wheel: never look at them again. A node that
             * vanishes and comes back is re-checked (see input_poll). */
            close(fd);
            input_checked[i] = true;
            continue;
        }

        name[0] = '\0';
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        name[sizeof(name) - 1] = '\0';

        /* The generic button driver maps NEXTSONG/PREVIOUSSONG backwards
         * for the H2's own buttons; don't let it see the car's keys. */
        if (i < BUTTON_NODES)
            button_remove_input_device(i);

        input_fd[i] = fd;
        keys_node_count++;
        avrcp_log("keys: using event%d \"%s\"", i, name);
    }
}

static void input_key(int code, int value)
{
    unsigned cmd = 0;

    if (value == 0)
        return; /* release */

    switch (code) {
    case KEY_PLAYCD:        cmd = CMD_PLAY;   break;
    case KEY_PAUSECD:       cmd = CMD_PAUSE;  break;
    case KEY_PLAYPAUSE:     cmd = CMD_TOGGLE; break;
    case KEY_STOPCD:
    case KEY_STOP:          cmd = CMD_STOP;   break;
    case KEY_NEXTSONG:      cmd = CMD_NEXT;   break;
    case KEY_PREVIOUSSONG:  cmd = CMD_PREV;   break;
    case KEY_FASTFORWARD:   cmd = CMD_FFWD;   break;
    case KEY_REWIND:        cmd = CMD_REW;    break;
    default:
        return;
    }

    if (value == 2) {
        /* Auto-repeat: only seeking repeats, and not faster than 2/s. */
        if (!(cmd & (CMD_FFWD | CMD_REW))
            || TIME_BEFORE(current_tick, last_seek_key_tick + HZ / 2))
            return;
    }
    if (cmd & (CMD_FFWD | CMD_REW))
        last_seek_key_tick = current_tick;

    avrcp_log("keys: code %d value %d", code, value);
    run_cmd(cmd);
}

static void input_poll(void)
{
    int i;

    for (i = 0; i < MAX_INPUT_NODES; i++) {
        struct input_event ev;
        ssize_t n;

        if (input_fd[i] < 0)
            continue;
        while ((n = read(input_fd[i], &ev, sizeof(ev))) == sizeof(ev)) {
            if (ev.type == EV_KEY)
                input_key(ev.code, ev.value);
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            /* ENODEV: car disconnected and BlueZ destroyed the device. */
            input_close(i);
            input_checked[i] = false;
        }
    }
}

/* Re-check nodes that went away (a BT node can reuse a built-in number). */
static void input_recheck_missing(void)
{
    int i;

    for (i = 0; i < MAX_INPUT_NODES; i++) {
        char path[32];
        if (!input_checked[i])
            continue;
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (access(path, F_OK) != 0)
            input_checked[i] = false;
    }
}

/* ------------------------------------------------------------------ */
/* Rockbox worker thread                                               */
/* ------------------------------------------------------------------ */

static long avrcp_stack[(DEFAULT_STACK_SIZE + 0x2000) / sizeof(long)];
static unsigned int avrcp_thread_id;
static volatile bool avrcp_run;

static void update_status_text(void)
{
    const char *keys = keys_node_count > 0 ? "car linked" : "waiting";

    if (!avrcp_run)
        snprintf(status_text, sizeof(status_text), "Car controls: off");
    else if (dbus_bluez_ver)
        snprintf(status_text, sizeof(status_text),
                 "Car: %s, info on", keys);
    else if (dbus_lib_missing)
        snprintf(status_text, sizeof(status_text),
                 "Car: %s, no info (dbus)", keys);
    else
        snprintf(status_text, sizeof(status_text), "Car: %s", keys);
}

static void avrcp_thread_fn(void)
{
    int tick = 0;

    avrcp_log("avrcp: worker start");
    while (avrcp_run) {
        unsigned cmds;

        if (tick % 10 == 0) {
            input_recheck_missing();
            input_scan();
        }
        input_poll();

        cmds = __sync_fetch_and_and(&pending_cmds, 0);
        if (cmds)
            run_cmd(cmds);

        if (tick % 5 == 0) {
            take_snapshot();
            update_status_text();
            if (!dbus_tid_valid && access(DBUS_SOCK_PATH, F_OK) == 0)
                dbus_thread_start();
        }

        tick++;
        sleep(HZ / 10);
    }

    for (tick = 0; tick < MAX_INPUT_NODES; tick++)
        input_close(tick);
    avrcp_log("avrcp: worker stop");
}

void bt_avrcp_start(void)
{
    int i;

    if (avrcp_run)
        return;

    for (i = 0; i < MAX_INPUT_NODES; i++) {
        input_fd[i] = -1;
        input_checked[i] = false;
    }
    keys_node_count = 0;
    avrcp_run = true;
    update_status_text();

    /* Rockbox threads exit by returning; recreate on every start. */
    avrcp_thread_id = create_thread(avrcp_thread_fn, avrcp_stack,
                                    sizeof(avrcp_stack), 0, "bt_avrcp"
                                    IF_PRIO(, PRIORITY_USER_INTERFACE)
                                    IF_COP(, CPU));
    if (!avrcp_thread_id) {
        avrcp_run = false;
        avrcp_log("avrcp: create_thread failed");
    }
}

void bt_avrcp_stop(void)
{
    if (!avrcp_run)
        return;
    avrcp_run = false;
    if (avrcp_thread_id) {
        thread_wait(avrcp_thread_id);
        avrcp_thread_id = 0;
    }
    dbus_thread_stop();
    __sync_fetch_and_and(&pending_cmds, 0);
    update_status_text();
}

const char *bt_avrcp_status_text(void)
{
    update_status_text();
    return status_text;
}
