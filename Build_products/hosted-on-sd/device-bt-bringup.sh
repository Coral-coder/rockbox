#!/bin/sh
# Hifi Walker H2 (HW4) stock Linux BT bring-up.
# HW4 BT bring-up for hosted Rockbox (stock Linux stack).
# Rockbox invokes this with an empty PATH; tools live in /usr/sbin.
export PATH=/usr/sbin:/usr/bin:/bin:/sbin
set -u
log() { echo "[bt-bringup] $*"; }

kill_stale() {
  killall bt-media bt-monitor bt-agent bluetoothd brcm_patchram_plus dbus-daemon 2>/dev/null || true
  rm -f /var/run/messagebus.pid
  hciconfig hci0 down 2>/dev/null || true
  echo 0 > /sys/class/rfkill/rfkill0/state 2>/dev/null || true
  sleep 1
}

start_dbus() {
  mkdir -p /tmp/dbus /var/lib/dbus /var/run/dbus
  test -f /var/lib/dbus/machine-id || dbus-uuidgen > /var/lib/dbus/machine-id
  if pidof dbus-daemon >/dev/null 2>&1; then
    test -S /var/run/dbus/system_bus_socket && return 0
  fi
  rm -f /var/run/dbus/system_bus_socket /var/run/messagebus.pid
  dbus-daemon --config-file=/etc/dbus-1/system.conf --fork
  sleep 1
  test -S /var/run/dbus/system_bus_socket
}

bt_radio_and_hci() {
  echo 1 > /sys/class/rfkill/rfkill0/state
  sleep 1
  BT_ADDR=$(sa_config bt_addr 2>/dev/null)
  test -n "$BT_ADDR" || BT_ADDR=DE:AD:BE:EF:00:00
  log "patchram bd_addr=$BT_ADDR"
  brcm_patchram_plus --enable_hci --baudrate 3000000 --no2bytes \
    --patchram /lib/firmware/BCM4343A1_001.002.009.0122.0538.hcd /dev/ttyS0 \
    --tosleep=50000 --use_baudrate_for_download --enable_lpm --bd_addr "$BT_ADDR" &
  sleep 5
  if [ ! -d /sys/class/bluetooth/hci0 ]; then
    log "hci0 missing after patchram"
    dmesg | tail -12
    return 1
  fi
  hciconfig hci0 up
  sleep 1
  return 0
}

start_userspace() {
  start_dbus || return 1
  bluetoothd &
  sleep 2
  hciconfig hci0 up 2>/dev/null || true
  sleep 1
  bt-agent &
  bt-monitor &
  bt-media &
  if [ -f /usr/resource/bt_name ]; then
    bt_name=$(cat /usr/resource/bt_name)
    bt-adapter --set Name "$bt_name" 2>/dev/null || true
  fi
  bt-adapter --set Powered On 2>/dev/null || true
  mkdir -p /tmp
  if pidof bluetoothd >/dev/null 2>&1 && pidof dbus-daemon >/dev/null 2>&1; then
    : > /tmp/bt_init_ok
  else
    log "bluetoothd or dbus-daemon missing; no bt_init_ok"
    return 1
  fi
  return 0
}

main() {
  log "teardown"
  kill_stale
  log "dbus"
  start_dbus || exit 1
  log "hci"
  bt_radio_and_hci || exit 2
  hciconfig -a
  log "stack"
  start_userspace || exit 3
  hciconfig hci0 piscan 2>/dev/null || true
  log "hcitool con:"
  hcitool con 2>/dev/null || true
  log "ALSA Output Port Switch (numid=4); Rockbox uses port 4 for BT"
  amixer -c 0 cget numid=4 2>/dev/null || true
  log "done"
}

main "$@"