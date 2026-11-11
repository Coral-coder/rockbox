/***************************************************************************
 * Hosted (HiBy Linux) Bluetooth AVRCP target: car / headset remote control
 * and now-playing metadata for the H2 (aigoerosq hosted build).
 ****************************************************************************/
#ifndef BLUETOOTH_AVRCP_HOSTED_H
#define BLUETOOTH_AVRCP_HOSTED_H

#include <stddef.h>

/* Start/stop the AVRCP worker (idempotent). Started with the BT stack. */
void bt_avrcp_start(void);
void bt_avrcp_stop(void);

/* One-line status for the Bluetooth menu, e.g. "Car: keys+info (BlueZ 4)". */
const char *bt_avrcp_status_text(void);

#endif
