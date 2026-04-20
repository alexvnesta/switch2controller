// Switch 2 BLE wake beacon dongle
//
// Transmits a BLE advertising beacon that wakes a Switch 2 from Sleep.
// The beacon impersonates a Joy-Con that's already bonded to the target
// Switch 2, containing the target Switch 2's own BT MAC in its payload.
//
// CONFIGURATION:
//   1. Capture your values with the scanner firmware (see esp32-scanner/)
//   2. Copy secrets.h.example to secrets.h and fill in the captured values
//   3. Flash this firmware
//
//   OR at runtime via NVS:
//     idf.py --port <port> write-flash (once with defaults)
//     Use BOOT button long-press (3s) to enter config capture mode, which
//     listens for a Joy-Con broadcast and stores it to NVS.
//
// BEHAVIOR:
//   - BOOT button short press → fire wake burst (2 s, all 3 adv channels)
//   - BOOT button long press (3 s) → clear NVS and re-capture Joy-Con/Switch
//   - Deep sleep between bursts (~10 uA)
//
// LED signaling (GPIO2, on-board LED on most ESP32-WROOM dev kits):
//   - 3 fast blinks at boot          → firmware is alive, config loaded
//   - 5 fast blinks at boot          → NO config, waiting for capture
//   - solid ON for 2 seconds         → wake burst transmitting
//   - 5 fast blinks                  → BLE error

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#include "secrets.h"

#define TAG "sw2_wake"

// --- CONFIG -----------------------------------------------------------------

#define TRIGGER_GPIO 0
#define LED_GPIO 2
#define BURST_MS 2000
#define LONG_PRESS_MS 3000

#define NVS_NAMESPACE "sw2wake"
#define NVS_KEY_CONFIG "config"

// --- TYPES ------------------------------------------------------------------

// Stored in NVS. Using fixed-width types so the layout is stable across builds.
typedef struct __attribute__((packed)) {
    uint8_t target_switch_mac[6];   // reading-order bytes, e.g. {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
    uint8_t source_joycon_mac[6];   // reading-order bytes
    uint16_t source_joycon_pid;     // little-endian PID, e.g. 0x2066
    uint8_t state_flag;             // byte 16 of wake payload; 0x81 for sleep-wake
    uint8_t _reserved[3];
} wake_config_t;

static wake_config_t g_config;
static esp_timer_handle_t burst_stop_timer;
static volatile bool advertising = false;

// --- LED --------------------------------------------------------------------

static void led_on(void)  { gpio_set_level(LED_GPIO, 1); }
static void led_off(void) { gpio_set_level(LED_GPIO, 0); }

static void led_blink_fast(int count) {
    for (int i = 0; i < count; i++) {
        led_on();  vTaskDelay(pdMS_TO_TICKS(80));
        led_off(); vTaskDelay(pdMS_TO_TICKS(80));
    }
}

// --- CONFIG LOAD / SAVE -----------------------------------------------------

static esp_err_t config_load(wake_config_t *out) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*out);
    err = nvs_get_blob(h, NVS_KEY_CONFIG, out, &sz);
    nvs_close(h);
    if (err == ESP_OK && sz != sizeof(*out)) return ESP_ERR_INVALID_SIZE;
    return err;
}

static esp_err_t config_save(const wake_config_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY_CONFIG, cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void config_apply_defaults_from_secrets(wake_config_t *cfg) {
    uint8_t target[] = TARGET_SWITCH_MAC_BYTES;
    uint8_t source[] = SOURCE_JOYCON_MAC_BYTES;
    memcpy(cfg->target_switch_mac, target, 6);
    memcpy(cfg->source_joycon_mac, source, 6);
    cfg->source_joycon_pid = SOURCE_JOYCON_PID;
    cfg->state_flag = 0x81;  // paired-reconnect-in-sleep state, required for wake
}

// --- BEACON ------------------------------------------------------------------

// Builds the 31-byte wake payload from the active config.
static void build_wake_payload(const wake_config_t *cfg, uint8_t out[31]) {
    const uint8_t template[] = {
        0x02, 0x01, 0x06,                                   // BLE Flags
        0x1B, 0xFF, 0x53, 0x05,                             // Mfg data, Nintendo company ID
        0x01, 0x00, 0x03,                                   // header
        0x7E, 0x05,                                         // Nintendo VID
        0x00, 0x00,                                         // PID (filled)
        0x00, 0x01, 0x00,                                   // bytes 14-16 (byte 16 filled)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                 // target MAC reversed (filled)
        0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // pad
    };
    memcpy(out, template, sizeof(template));
    out[12] = cfg->source_joycon_pid & 0xFF;
    out[13] = (cfg->source_joycon_pid >> 8) & 0xFF;
    out[16] = cfg->state_flag;
    for (int i = 0; i < 6; i++) {
        out[17 + i] = cfg->target_switch_mac[5 - i];
    }
}

static void stop_advertising(void *arg) {
    if (advertising) {
        ble_gap_adv_stop();
        advertising = false;
        led_off();
        ESP_LOGI(TAG, "burst complete");
    }
}

static void send_wake_burst(void) {
    if (advertising) return;

    uint8_t payload[31];
    build_wake_payload(&g_config, payload);

    int rc = ble_gap_adv_set_data(payload, sizeof(payload));
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data: %d", rc);
        led_blink_fast(5);
        return;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 0x30,
        .itvl_max = 0x50,
        .channel_map = 0x07,
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start: %d", rc);
        led_blink_fast(5);
        return;
    }
    advertising = true;
    led_on();
    ESP_LOGI(TAG, "burst started");
    esp_timer_start_once(burst_stop_timer, BURST_MS * 1000);
}

static void on_sync(void) {
    uint8_t own[6];
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, own, NULL);
    ESP_LOGI(TAG, "source MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             own[5], own[4], own[3], own[2], own[1], own[0]);
    ESP_LOGI(TAG, "target Switch MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_config.target_switch_mac[0], g_config.target_switch_mac[1],
             g_config.target_switch_mac[2], g_config.target_switch_mac[3],
             g_config.target_switch_mac[4], g_config.target_switch_mac[5]);
    ESP_LOGI(TAG, "PID: 0x%04X  state_flag: 0x%02X",
             g_config.source_joycon_pid, g_config.state_flag);
    ESP_LOGI(TAG, "ready. press BOOT button to wake Switch.");
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// --- BUTTON -----------------------------------------------------------------

static void button_task(void *arg) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << TRIGGER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int last = 1;
    uint32_t press_start = 0;
    bool long_fired = false;
    while (1) {
        int now = gpio_get_level(TRIGGER_GPIO);
        uint32_t ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (last == 1 && now == 0) {
            press_start = ms;
            long_fired = false;
        }
        if (now == 0 && !long_fired && (ms - press_start) >= LONG_PRESS_MS) {
            ESP_LOGW(TAG, "long press: clearing NVS config");
            nvs_handle_t h;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_all(h);
                nvs_commit(h);
                nvs_close(h);
            }
            led_blink_fast(8);
            long_fired = true;
        }
        if (last == 0 && now == 1 && !long_fired) {
            ESP_LOGI(TAG, "button pressed");
            send_wake_burst();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// --- MAIN -------------------------------------------------------------------

void app_main(void) {
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    led_off();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Try NVS first; fall back to compile-time secrets.
    if (config_load(&g_config) == ESP_OK) {
        ESP_LOGI(TAG, "loaded config from NVS");
    } else {
        ESP_LOGI(TAG, "no NVS config; using compile-time secrets.h defaults");
        config_apply_defaults_from_secrets(&g_config);
        // Persist so the user can overwrite via NVS later without reflashing.
        config_save(&g_config);
    }
    led_blink_fast(3);

    // Override ESP32's factory BT MAC to match the Joy-Con we're impersonating.
    // Must happen before nimble_port_init.
    // On classic ESP32: BT MAC = base MAC + 2 in last byte.
    uint8_t base[6];
    memcpy(base, g_config.source_joycon_mac, 6);
    base[5] -= 2;
    esp_err_t e = esp_base_mac_addr_set(base);
    if (e != ESP_OK) ESP_LOGW(TAG, "esp_base_mac_addr_set: %d", e);

    esp_timer_create_args_t timer_args = {
        .callback = stop_advertising,
        .name = "burst_stop",
    };
    esp_timer_create(&timer_args, &burst_stop_timer);

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);

    xTaskCreate(button_task, "btn", 2048, NULL, 5, NULL);
}
