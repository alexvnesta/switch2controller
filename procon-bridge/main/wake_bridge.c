/*
 * Switch 2 wake bridge — Pro Controller HOME-press → Switch 2 BLE wake beacon
 *
 * See wake_bridge.h for protocol overview. Payload structure documented in
 * docs/wake-packet.md.
 */

#include <stdint.h>
#include <string.h>
#include <esp_timer.h>
#include "sdkconfig.h"
#include "wake_bridge.h"
#include "bluetooth/hci.h"

/* === CONFIG ================================================================
 * Compile-time config. Replace these with your own captured values from
 * esp32/src/secrets.h, OR with NVS-loaded values once we add that path.
 *
 * To find these: see esp32/README.md and docs/scanner-guide.md.
 */
#ifndef WAKE_TARGET_SWITCH_MAC
/* Reading-order bytes of the target Switch 2's BT MAC address.
 * E.g. for 98:E2:55:99:1F:97 → {0x98,0xE2,0x55,0x99,0x1F,0x97}.
 * Replace with YOUR Switch 2's MAC. The all-zero default WILL NOT WAKE anything.
 */
#define WAKE_TARGET_SWITCH_MAC { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

#ifndef WAKE_PID
/* PID byte to advertise. Switch 2 wake filter accepts:
 *   0x2066 (Joy-Con 2 L), 0x2067 (Joy-Con 2 R), 0x2069 (Pro Controller 2)
 * Per ndeadly/switch2_controller_research/bluetooth_interface.md.
 */
#define WAKE_PID 0x2066
#endif

#ifndef WAKE_BURST_MS
#define WAKE_BURST_MS 2000
#endif

/* === STATE ================================================================ */

static const uint8_t TARGET_SWITCH_MAC[6] = WAKE_TARGET_SWITCH_MAC;

static esp_timer_handle_t s_burst_stop_timer;
static volatile bool s_advertising = false;
static volatile bool s_home_was_pressed = false;
static int64_t s_last_burst_us = 0;

#define MIN_REBURST_INTERVAL_US (3 * 1000 * 1000)  /* 3s debounce between bursts */

/* === PAYLOAD CONSTRUCTION =================================================
 * 31-byte BLE adv payload, matches docs/wake-packet.md exactly.
 *
 * [02 01 06] [1B FF] [53 05] [01 00 03] [7E 05] [PID_lo PID_hi]
 * [00 01 81] [target_mac reversed × 6] [0F] [00×7]
 */
static void build_wake_payload(uint8_t out[31]) {
    static const uint8_t TEMPLATE[31] = {
        0x02, 0x01, 0x06,                                /* Flags */
        0x1B, 0xFF,                                      /* Mfr data hdr */
        0x53, 0x05,                                      /* Nintendo company ID 0x0553 */
        0x01, 0x00, 0x03,                                /* Constant */
        0x7E, 0x05,                                      /* Nintendo VID 0x057E */
        0x00, 0x00,                                      /* PID (filled below) */
        0x00, 0x01, 0x81,                                /* state flag = 0x81 (wake) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              /* target MAC reversed (filled) */
        0x0F,                                            /* Constant trailer */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* Reserved zeros */
    };
    memcpy(out, TEMPLATE, sizeof(TEMPLATE));
    out[12] = (uint8_t)(WAKE_PID & 0xFF);
    out[13] = (uint8_t)((WAKE_PID >> 8) & 0xFF);
    for (int i = 0; i < 6; i++) {
        out[17 + i] = TARGET_SWITCH_MAC[5 - i];
    }
}

/* === BURST CONTROL ======================================================== */

static void stop_burst_cb(void *arg) {
    if (s_advertising) {
        bt_hci_cmd_le_adv_disable();
        s_advertising = false;
        printf("# wake_bridge: burst complete\n");
    }
}

static void fire_wake_burst(void) {
    int64_t now = esp_timer_get_time();
    if (s_advertising) return;
    if (now - s_last_burst_us < MIN_REBURST_INTERVAL_US) {
        printf("# wake_bridge: burst suppressed (debounce)\n");
        return;
    }
    s_last_burst_us = now;

    uint8_t payload[31];
    build_wake_payload(payload);

    /* Sanity check: don't TX an all-zero target MAC. */
    bool zero_mac = true;
    for (int i = 0; i < 6; i++) if (TARGET_SWITCH_MAC[i]) { zero_mac = false; break; }
    if (zero_mac) {
        printf("# wake_bridge: TARGET_SWITCH_MAC is all-zero, NOT firing burst.\n");
        printf("# wake_bridge: Set WAKE_TARGET_SWITCH_MAC at build time. See wake_bridge.c.\n");
        return;
    }

    printf("# wake_bridge: firing wake burst, target %02X:%02X:%02X:%02X:%02X:%02X PID=0x%04X\n",
           TARGET_SWITCH_MAC[0], TARGET_SWITCH_MAC[1], TARGET_SWITCH_MAC[2],
           TARGET_SWITCH_MAC[3], TARGET_SWITCH_MAC[4], TARGET_SWITCH_MAC[5],
           WAKE_PID);

    bt_hci_cmd_le_set_adv_data_raw(payload, 31);
    s_advertising = true;
    esp_timer_start_once(s_burst_stop_timer, WAKE_BURST_MS * 1000);
}

/* === PUBLIC API =========================================================== */

void wake_bridge_init(void) {
    const esp_timer_create_args_t args = {
        .callback = stop_burst_cb,
        .name = "wake_burst_stop",
    };
    esp_timer_create(&args, &s_burst_stop_timer);
    s_advertising = false;
    s_home_was_pressed = false;
    s_last_burst_us = 0;
    printf("# wake_bridge: initialized\n");
}

void wake_bridge_on_input(uint8_t report_id, const uint8_t *data, uint32_t len) {
    /* Switch Pro Controller standard input report (0x30):
     *   data[0..2]: timer/battery/conn info
     *   data[3]:    Y X B A SR_R SL_R R ZR
     *   data[4]:    - + Rstk Lstk HOME CAPTURE _ CHRG_GRIP
     *   data[5]:    D L U R SR_L SL_L L ZL
     *   data[6+]:   stick & IMU data
     *
     * Per dekuNukem/Nintendo_Switch_Reverse_Engineering/bluetooth_hid_notes.md.
     * HOME = bit 4 of byte 4. CAPTURE = bit 5 of byte 4.
     */
    if (report_id == 0x30 && len >= 5) {
        bool home_now = (data[4] & 0x10) != 0;
        if (home_now && !s_home_was_pressed) {
            printf("# wake_bridge: HOME pressed\n");
            fire_wake_burst();
        }
        s_home_was_pressed = home_now;
        return;
    }

    /* Simple HID report (0x3F): used during init / fallback mode.
     *   data[0]: button bits 0-7  (Y B A X L R ZL ZR)
     *   data[1]: button bits 8-15 (- + LSTK RSTK HOME CAPTURE _ _)
     *   data[2]: HAT
     *   data[3..10]: stick analog
     *
     * HOME = bit 4 of byte 1.
     */
    if (report_id == 0x3F && len >= 2) {
        bool home_now = (data[1] & 0x10) != 0;
        if (home_now && !s_home_was_pressed) {
            printf("# wake_bridge: HOME pressed (simple report)\n");
            fire_wake_burst();
        }
        s_home_was_pressed = home_now;
        return;
    }
}
