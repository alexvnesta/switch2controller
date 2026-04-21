/*
 * Switch 2 wake bridge — Pro Controller HOME-press → Switch 2 BLE wake beacon
 *
 * When CONFIG_PROCON_WAKE_BRIDGE is set, bt_host_bridge() routes Switch
 * Pro Controller input reports here instead of into the wired-adapter pipeline.
 *
 * On HOME-button-press edge, fires a BLE wake beacon shaped to wake the
 * configured Switch 2 from sleep. Beacon construction matches the spec in
 * docs/wake-packet.md and reuses BlueRetro's HCI LE-adv command path.
 */

#ifndef _WAKE_BRIDGE_H_
#define _WAKE_BRIDGE_H_

#include <stdint.h>

/* Initialize the wake bridge. Call once from the BT host init path,
 * after the BT controller is up but before adv data is sent. Loads
 * configured Switch 2 MAC + PID from compile-time defaults (and
 * eventually NVS).
 */
void wake_bridge_init(void);

/* Called from bt_host_bridge() when CONFIG_PROCON_WAKE_BRIDGE is set.
 * report_id and data are the raw HID input report from the Pro Controller.
 *
 * Switch Pro Controller input reports of interest:
 *   0x30 - Standard full controller state (every ~15 ms when in standard mode)
 *   0x3F - Simple HID report (less common, used during init)
 *
 * In report 0x30, button bytes are at data[3..5]:
 *   data[3]: Y X B A SR_R SL_R R ZR
 *   data[4]: - + Rstk Lstk HOME CAPTURE _ CHRG_GRIP
 *   data[5]: D L U R SR_L SL_L L ZL
 *
 * HOME = bit 4 of data[4].
 */
void wake_bridge_on_input(uint8_t report_id, const uint8_t *data, uint32_t len);

#endif
