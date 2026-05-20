/***************************************************************************
 * Carplay_Direct lab menu — run on-device USB/framebuffer audit scripts.
 ****************************************************************************/

#include "config.h"
#include "lang.h"
#include "menu.h"
#include "splash.h"
#include "kernel.h"
#include "gui/list.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define CP_LOG_TMP          "/tmp/rb_carplay_lab.log"
#define CP_LOG_SD           "/mnt/sd_0/.rockbox/carplay/last-run.log"
#define CP_SCRIPT_USB       "/mnt/sd_0/.rockbox/carplay/usb-discovery-device.sh"
#define CP_SCRIPT_FB        "/mnt/sd_0/.rockbox/carplay/fb-discovery-device.sh"
#define CP_SCRIPT_GADGET    "/mnt/sd_0/.rockbox/carplay/usb-gadget-lab-device.sh"

static char cp_status[80] = "CarPlay lab: idle";

static void cp_log_line(const char *msg)
{
    FILE *f;
    int i;
    const char *paths[] = { CP_LOG_TMP, CP_LOG_SD };

    for (i = 0; i < 2; i++) {
        f = fopen(paths[i], "a");
        if (!f)
            continue;
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

static bool cp_path_ok(const char *path)
{
    return access(path, F_OK) == 0;
}

static int cp_run_script(const char *script, const char *label)
{
    char cmd[256];
    int st;

    if (!cp_path_ok(script)) {
        snprintf(cp_status, sizeof(cp_status), "Missing: %.32s", script);
        cp_log_line(cp_status);
        splashf(HZ * 2, "Copy scripts to SD /.rockbox/carplay/");
        return 0;
    }

    snprintf(cmd, sizeof(cmd),
             "sh %s >> %s 2>&1; echo DONE >> %s",
             script, CP_LOG_TMP, CP_LOG_TMP);
    snprintf(cp_status, sizeof(cp_status), "Running %s", label);
    cp_log_line(cp_status);

    st = system(cmd);
    snprintf(cp_status, sizeof(cp_status), "%s exit %d", label, st);
    cp_log_line(cp_status);
    splashf(HZ * 2, "%s", cp_status);
    return 0;
}

static int cp_usb_audit(void)
{
    return cp_run_script(CP_SCRIPT_USB, "USB audit");
}

static int cp_fb_audit(void)
{
    return cp_run_script(CP_SCRIPT_FB, "FB audit");
}

static int cp_show_udc(void)
{
    FILE *f;
    char line[96];

    f = fopen("/sys/class/udc", "r");
    if (!f) {
        snprintf(cp_status, sizeof(cp_status), "No UDC sysfs");
        splashf(HZ * 2, "%s", cp_status);
        return 0;
    }
    if (!fgets(line, sizeof(line), f)) {
        snprintf(cp_status, sizeof(cp_status), "UDC: (empty)");
    } else {
        line[strcspn(line, "\n")] = '\0';
        snprintf(cp_status, sizeof(cp_status), "UDC: %.48s", line);
    }
    fclose(f);
    cp_log_line(cp_status);
    splashf(HZ * 2, "%s", cp_status);
    return 0;
}

static int cp_show_android_usb(void)
{
    FILE *f;
    char buf[32];

    f = fopen("/sys/class/android_usb/android0/functions", "r");
    if (!f) {
        snprintf(cp_status, sizeof(cp_status), "No android_usb");
        splashf(HZ * 2, "%s", cp_status);
        return 0;
    }
    if (!fgets(buf, sizeof(buf), f)) {
        strcpy(buf, "?");
    } else {
        buf[strcspn(buf, "\n")] = '\0';
    }
    fclose(f);
    snprintf(cp_status, sizeof(cp_status), "USB fn: %s", buf);
    cp_log_line(cp_status);
    splashf(HZ * 2, "%s", cp_status);
    return 0;
}

static int cp_gadget_audit(void)
{
    return cp_run_script(CP_SCRIPT_GADGET, "Gadget lab");
}

MENUITEM_FUNCTION(cp_item_usb, 0, "Run USB discovery",
                  cp_usb_audit, NULL, Icon_NOICON);
MENUITEM_FUNCTION(cp_item_fb, 0, "Run framebuffer audit",
                  cp_fb_audit, NULL, Icon_NOICON);
MENUITEM_FUNCTION(cp_item_udc, 0, "Show UDC name",
                  cp_show_udc, NULL, Icon_NOICON);
MENUITEM_FUNCTION(cp_item_fn, 0, "Show android_usb fn",
                  cp_show_android_usb, NULL, Icon_NOICON);
MENUITEM_FUNCTION(cp_item_gadget, 0, "Run gadget lab",
                  cp_gadget_audit, NULL, Icon_NOICON);

MAKE_MENU(carplay_lab_menu, "CarPlay lab", NULL, Icon_NOICON,
          &cp_item_usb, &cp_item_fb, &cp_item_gadget, &cp_item_udc, &cp_item_fn);
