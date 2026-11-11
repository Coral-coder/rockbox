# Car controls (CarPlay / Android Auto cars) and audiobooks — H2 hosted

## Car controls over Bluetooth

The H2 cannot appear *as* CarPlay or Android Auto. Both protocols need the
phone side to pass Apple/Google certificate checks that only real phones can
do (see the `Carplay_Direct` branch for the USB research). What does work in
every CarPlay / Android Auto car is the car's normal **Bluetooth audio**
source. Rockbox now acts as a full Bluetooth media player there:

| From the car (steering wheel / screen) | Rockbox does |
|---|---|
| Play / Pause | play or pause (Play never toggles to pause) |
| Stop (often sent on source change) | pause, so your place is kept |
| Next | next chapter (M4B / CUE), otherwise next track |
| Previous | restart chapter/track; within 3 s → previous one |
| Fast-forward / Rewind | +30 s / −15 s (held: repeats twice a second) |

The car screen shows **title, artist, album, track number and position**.
For books with chapters it shows the **chapter** title and chapter-relative
time, with the book as the album.

### Use it

1. Settings → Bluetooth → Bluetooth **on**.
2. Pair from the car: the H2 is discoverable while Bluetooth is on (or use
   *Scan for devices* on the H2 and pick the car).
3. In the car, choose Bluetooth audio as the source. With CarPlay/AA, the
   phone must not also be streaming. Disconnect the phone or pick the H2 as
   the audio device.

The Bluetooth menu has a status line:

* `Car: car linked, info on`: buttons and now-playing info both work.
* `Car: waiting`: no AVRCP remote is connected yet.
* `Car: ..., no info (dbus)`: buttons work but `libdbus-1` could not be
  loaded, so the car can't show track info.

### How it works / troubleshooting

`apps/menus/bluetooth_avrcp_hosted.c`

* **Buttons:** BlueZ creates a uinput key device (bus type Bluetooth) for
  each AVRCP remote. Rockbox reads it and posts `BUTTON_MULTIMEDIA_*` events,
  which are handled from any screen. This needs `/dev/uinput` in the stock
  kernel.
* **Track info:** Rockbox registers a media player with `bluetoothd` over the
  system D-Bus, trying BlueZ 5 (`org.bluez.Media1`) first and then BlueZ 4
  (`org.bluez.Media`). libdbus is `dlopen()`ed at runtime, so the build needs
  no D-Bus headers. On BlueZ 5, button presses arrive as MPRIS method calls
  instead of uinput keys; both paths are handled.
* Log: `/tmp/rb_bt_avrcp.log`, copied to `/.rockbox/rb_bt_avrcp.log` on the
  SD card. Lines like `keys: using event3 "..."` and
  `dbus: media player registered (BlueZ 4)` confirm each half is working.

## Audiobooks

* **M4B / M4A chapters:** Rockbox now reads the Nero chapter list (`chpl`)
  that ffmpeg, m4b-tool, AudioBookConverter and mp4chaps write by default.
  Chapters behave like CUE tracks: chapter title on the WPS, next/prev by
  chapter (buttons and car), chapter marks on the progress bar, and a chapter
  list from the WPS context menu. Up to 99 chapters per file are used.
  **Turn on Settings → Playback Settings → Cuesheet Support** (it is off by
  default and also enables chapters).
  Files that only have QuickTime text-track chapters (no `chpl`) are not
  supported yet. Re-mux them with
  `ffmpeg -i in.m4b -map 0 -map_chapters 0 -c copy out.m4b`, which writes `chpl`.
* **Resume each book where you left off:** Settings → General Settings →
  Automatic Resume → *Automatic Resume: Yes*. Set *Resume on automatic track
  change* to *In custom directories only*, then put your book folder in
  `/.rockbox/config.cfg` as `autoresume next track paths: /Audiobooks`
  (the default is `/podcast:/podcasts`). This needs the database (initialise
  it once).
* **Rewind a little on pause:** Settings → Playback Settings →
  *Rewind on Pause*.
* **Speed:** Settings → Playback Settings → *Timestretch* on, then change
  speed on the Pitch screen.
