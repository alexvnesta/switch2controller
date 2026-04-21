// Switch 1 Pro Controller wake-trigger scanner
//
// Purpose: detect BLE advertisements emitted by a Switch 1 Pro Controller
// when it tries to wake a paired host. Used to run the test matrix in
// docs/procon-trigger-test.md, which determines under what controller
// states the wake adv actually fires.
//
// Differences from ble_scanner.c.disabled:
//   - PASSIVE scan (no scan_req, since we want to observe stock behavior
//     without provoking scan responses)
//   - Logs ALL adv events from a configurable target MAC (PROCON_MAC),
//     plus any Nintendo-OUI device for context
//   - Always logs the PDU type (ADV_IND / ADV_DIRECT_IND / etc.) since
//     PDU type is the key discriminator we're looking for
//   - Tighter raw-byte dump (one line, full payload)
//   - Per-event timestamp in ms since boot, so burst windows are measurable
//
// Configuration:
//   Set PROCON_MAC below to your Pro Controller's BT MAC. To find it:
//     1. Pair the Pro Controller to a Mac/PC over classic BT once
//     2. Read its MAC from the OS Bluetooth settings
//     3. Enter as 6 bytes in big-endian / reading order
//   If you don't know the MAC yet, leave it as zeros — the scanner will
//   log every BLE adv it sees so you can spot Nintendo-OUI candidates.
//
// LED: solid ON while scanning, briefly blinks on every Pro Controller hit.

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#define TAG "procon_scan"
#define LED_GPIO 2

// Set this to your Pro Controller's BT MAC, in reading order (big-endian).
// Example: { 0xE3, 0xC0, 0x0F, 0x40, 0xE5, 0x89 } for E3:C0:0F:40:E5:89.
// If all-zero, every BLE adv is logged (discovery mode).
//
// Set this to your Pro Controller's BT MAC, in reading order. Zero = discovery mode.
//
// 2026-04-20 test result: with PROCON_MAC = 98:B6:E9:3F:D4:FB OR all-zero discovery,
// the Switch 1 Pro Controller produced ZERO BLE adv events across multiple HOME-press
// cycles. Classic-BT page (BR/EDR baseband) is used for wake/reconnect, not BLE adv.
// See docs/procon-trigger-test-results.md.
static const uint8_t PROCON_MAC[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static bool procon_mac_set(void) {
    for (int i = 0; i < 6; i++) if (PROCON_MAC[i]) return true;
    return false;
}

static bool addr_matches_procon(const uint8_t *mac_reversed) {
    // NimBLE gives addr.val in reversed (little-endian on-air) order.
    // Compare against PROCON_MAC (reading order) by reversing comparison.
    for (int i = 0; i < 6; i++) {
        if (mac_reversed[i] != PROCON_MAC[5 - i]) return false;
    }
    return true;
}

static bool is_nintendo_oui(const uint8_t *mac_reversed) {
    // Common Nintendo OUIs: 98:B6:E9, E0:EF:BF, 04:03:D6, 7C:BB:8A, 34:AF:2C
    // mac_reversed[5..3] holds the OUI in reading order.
    uint8_t a = mac_reversed[5], b = mac_reversed[4], c = mac_reversed[3];
    return (a == 0x98 && b == 0xB6 && c == 0xE9) ||
           (a == 0xE0 && b == 0xEF && c == 0xBF) ||
           (a == 0x04 && b == 0x03 && c == 0xD6) ||
           (a == 0x7C && b == 0xBB && c == 0x8A) ||
           (a == 0x34 && b == 0xAF && c == 0x2C);
}

static const char *evt_name(uint8_t event_type) {
    switch (event_type) {
        case 0x00: return "ADV_IND";
        case 0x01: return "ADV_DIRECT_IND";  // What Pro Controller likely uses
        case 0x02: return "ADV_SCAN_IND";
        case 0x03: return "ADV_NONCONN_IND";
        case 0x04: return "SCAN_RSP";
        default:   return "?";
    }
}

static void led_pulse(void) {
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LED_GPIO, 1);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGI(TAG, "scan complete reason=%d", event->disc_complete.reason);
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_gap_disc_desc *d = &event->disc;
    const uint8_t *mac = d->addr.val;  // reversed from reading order

    bool is_procon = procon_mac_set() && addr_matches_procon(mac);
    bool is_nin    = is_nintendo_oui(mac);

    // Filter: if PROCON_MAC is set, log ONLY procon hits + nintendo OUIs.
    //         If PROCON_MAC is zero (discovery mode), log everything.
    if (procon_mac_set() && !is_procon && !is_nin) return 0;

    char marker = is_procon ? '*' : (is_nin ? '+' : '.');
    int64_t t_ms = esp_timer_get_time() / 1000;

    ESP_LOGW(TAG, "%c t=%lld  %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d  type=%d  evt=%s  len=%d",
             marker, t_ms,
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0],
             d->rssi, d->addr.type, evt_name(d->event_type), d->length_data);

    if (is_procon) led_pulse();

    // Raw byte dump — single line, full payload
    char buf[256];
    int pos = 0;
    for (int i = 0; i < d->length_data && pos < (int)sizeof(buf) - 3; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", d->data[i]);
    }
    if (d->length_data > 0) printf("    raw: %s\n", buf);

    return 0;
}

static void on_sync(void) {
    struct ble_gap_disc_params params = {
        .itvl = 0x0040,             // 40 ms scan interval
        .window = 0x0040,           // 40 ms window — 100% duty cycle to maximize catch rate
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,               // PASSIVE — observe only, don't send scan_req
        .filter_duplicates = 0,     // see every burst, don't dedupe
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return;
    }
    gpio_set_level(LED_GPIO, 1);

    if (procon_mac_set()) {
        ESP_LOGI(TAG, "scanning for Pro Controller %02X:%02X:%02X:%02X:%02X:%02X",
                 PROCON_MAC[0], PROCON_MAC[1], PROCON_MAC[2],
                 PROCON_MAC[3], PROCON_MAC[4], PROCON_MAC[5]);
        ESP_LOGI(TAG, "(* = procon hit, + = other Nintendo OUI)");
    } else {
        ESP_LOGI(TAG, "PROCON_MAC not set — discovery mode, logging ALL BLE adv");
    }
    ESP_LOGI(TAG, "run the test matrix in docs/procon-trigger-test.md");
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 0);

    // 3 boot blinks
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_GPIO, 1); vTaskDelay(pdMS_TO_TICKS(80));
        gpio_set_level(LED_GPIO, 0); vTaskDelay(pdMS_TO_TICKS(80));
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}
