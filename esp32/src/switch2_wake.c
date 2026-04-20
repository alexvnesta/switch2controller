// Switch 2 BLE wake beacon + Switch 1 Pro Controller bridge
//
// Goal: press HOME on a Switch 1 Pro Controller → wake Switch 2 from Sleep.
//
// Architecture (Bluedroid dual-mode):
//   1. Classic BT: pair with Pro Controller, receive HID input reports
//   2. BLE: when HOME pressed, transmit wake beacon to Switch 2
//
// LED signaling (GPIO2):
//   - 3 fast blinks at boot          → firmware alive, config loaded
//   - slow 1Hz blink                 → waiting for Pro Controller pairing
//   - short blip every 10s           → Pro Controller connected, polling
//   - solid ON for 2 seconds         → wake burst transmitting
//   - 5 fast blinks                  → BLE error
//
// BOOT button:
//   - short press: manually fire wake burst (without Pro Controller needed)
//   - long press (3s): clear NVS (wake config + classic BT link keys)

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_hidh_api.h"

#include "secrets.h"

#define TAG "sw2_wake"

// --- CONFIG -----------------------------------------------------------------

#define TRIGGER_GPIO 0
#define LED_GPIO 2
#define BURST_MS 2000
#define LONG_PRESS_MS 3000

#define NVS_NAMESPACE "sw2wake"
#define NVS_KEY_CONFIG "config"

typedef struct __attribute__((packed)) {
    uint8_t target_switch_mac[6];
    uint8_t source_joycon_mac[6];
    uint16_t source_joycon_pid;
    uint8_t state_flag;
    uint8_t _reserved[3];
} wake_config_t;

static wake_config_t g_config;
static esp_timer_handle_t burst_stop_timer;
static volatile bool ble_advertising = false;
static bool hid_connected = false;
static esp_bd_addr_t pro_controller_bda;
static uint16_t last_buttons;

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
    cfg->state_flag = 0x81;
}

// --- WAKE BEACON (Bluedroid BLE) --------------------------------------------

static uint8_t wake_payload[31];

static void build_wake_payload(const wake_config_t *cfg, uint8_t out[31]) {
    const uint8_t tmpl[] = {
        0x02, 0x01, 0x06,
        0x1B, 0xFF, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7E, 0x05,
        0x00, 0x00,                              // PID (filled)
        0x00, 0x01, 0x00,                        // byte 16 (filled)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,      // target MAC reversed (filled)
        0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(out, tmpl, sizeof(tmpl));
    out[12] = cfg->source_joycon_pid & 0xFF;
    out[13] = (cfg->source_joycon_pid >> 8) & 0xFF;
    out[16] = cfg->state_flag;
    for (int i = 0; i < 6; i++) {
        out[17 + i] = cfg->target_switch_mac[5 - i];
    }
}

static void stop_advertising(void *arg) {
    if (ble_advertising) {
        esp_ble_gap_stop_advertising();
        ble_advertising = false;
        led_off();
        ESP_LOGI(TAG, "burst complete");
    }
}

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
            struct ble_gap_adv_params_s {
                uint16_t adv_int_min, adv_int_max;
                esp_ble_adv_type_t adv_type;
                esp_ble_addr_type_t own_addr_type;
                esp_bd_addr_t peer_addr;
                esp_ble_addr_type_t peer_addr_type;
                esp_ble_adv_channel_t channel_map;
                esp_ble_adv_filter_t adv_filter_policy;
            };
            esp_ble_adv_params_t adv_params = {
                .adv_int_min = 0x30,
                .adv_int_max = 0x50,
                .adv_type = ADV_TYPE_NONCONN_IND,
                .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                .channel_map = ADV_CHNL_ALL,
                .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
            };
            esp_ble_gap_start_advertising(&adv_params);
            break;
        }
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ble_advertising = true;
                led_on();
                ESP_LOGI(TAG, "burst started");
                esp_timer_start_once(burst_stop_timer, BURST_MS * 1000);
            } else {
                ESP_LOGE(TAG, "adv start failed: %d", param->adv_start_cmpl.status);
                led_blink_fast(5);
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ble_advertising = false;
            break;
        default:
            break;
    }
}

static void send_wake_burst(void) {
    if (ble_advertising) return;
    build_wake_payload(&g_config, wake_payload);
    esp_err_t rc = esp_ble_gap_config_adv_data_raw(wake_payload, sizeof(wake_payload));
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "adv data set: %s", esp_err_to_name(rc));
        led_blink_fast(5);
    }
    // start_advertising called from ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT
}

// --- CLASSIC BT GAP (Pro Controller discovery/pairing) ----------------------

static bool eir_contains_pro_controller(uint8_t *eir, uint8_t eir_len) {
    if (!eir) return false;
    uint8_t len;
    uint8_t *name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
    if (!name) name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
    if (!name || len == 0) return false;
    // "Pro Controller" is what Switch 1 Pro Controller advertises
    return (len >= 14 && memcmp(name, "Pro Controller", 14) == 0);
}

static void bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            bool is_pro = false;
            char *name = NULL;
            uint8_t name_len = 0;
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR && eir_contains_pro_controller(p->val, p->len)) {
                    is_pro = true;
                }
                if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    name = (char *)p->val;
                    name_len = p->len;
                    if (name_len >= 14 && memcmp(name, "Pro Controller", 14) == 0) is_pro = true;
                }
            }
            uint8_t *a = param->disc_res.bda;
            ESP_LOGI(TAG, "discovered %02X:%02X:%02X:%02X:%02X:%02X  name=%.*s  pro=%d",
                     a[0], a[1], a[2], a[3], a[4], a[5],
                     name_len, name ? name : "(none)", is_pro);
            if (is_pro && !hid_connected) {
                memcpy(pro_controller_bda, a, sizeof(esp_bd_addr_t));
                esp_bt_gap_cancel_discovery();
                ESP_LOGI(TAG, "found Pro Controller, connecting HID");
                esp_bt_hid_host_connect(pro_controller_bda);
            }
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED && !hid_connected) {
                ESP_LOGI(TAG, "discovery stopped; restarting");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
            }
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "auth succeeded with %.*s",
                         (int)strlen((char *)param->auth_cmpl.device_name),
                         param->auth_cmpl.device_name);
            } else {
                ESP_LOGE(TAG, "auth failed: %d", param->auth_cmpl.stat);
            }
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            // Pro Controller uses JUST_WORKS normally, but some older revs
            // fall back to PIN "0000"
            ESP_LOGW(TAG, "PIN request — replying 0000");
            esp_bt_pin_code_t pin = {'0','0','0','0'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
            break;
        default:
            break;
    }
}

// --- HID HOST ---------------------------------------------------------------

static void hid_host_event_handler(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param) {
    switch (event) {
        case ESP_HIDH_INIT_EVT:
            ESP_LOGI(TAG, "HID host init done");
            break;
        case ESP_HIDH_OPEN_EVT:
            if (param->open.status == ESP_HIDH_OK) {
                hid_connected = true;
                ESP_LOGI(TAG, "HID connected");
            } else {
                ESP_LOGE(TAG, "HID open failed: %d", param->open.status);
            }
            break;
        case ESP_HIDH_CLOSE_EVT:
            hid_connected = false;
            ESP_LOGW(TAG, "HID closed; resuming inquiry");
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
            break;
        case ESP_HIDH_DATA_IND_EVT: {
            uint8_t *d = param->data_ind.data;
            uint16_t len = param->data_ind.len;
            if (len < 4) break;

            // Pro Controller "basic HID" report 0x3F has buttons at offset 1-2.
            // The 0x30 "standard" report has buttons at offset 3-5 (3 bytes).
            // For MVP we handle only 0x3F first (that's what the controller
            // sends by default on first connect before subcommand init).
            if (d[0] == 0x3f && len >= 3) {
                uint16_t btns = d[1] | (d[2] << 8);
                if (btns != last_buttons) {
                    ESP_LOGI(TAG, "buttons=0x%04X", btns);
                    // HOME in 0x3F mode: bit 12 (0x1000) of the 16-bit word
                    bool home_now = btns & 0x1000;
                    bool home_was = last_buttons & 0x1000;
                    if (home_now && !home_was) {
                        ESP_LOGW(TAG, "HOME pressed → wake burst");
                        send_wake_burst();
                    }
                    last_buttons = btns;
                }
            } else {
                // Log unknown report IDs once so we can learn the protocol
                static uint8_t seen_reports[16] = {0};
                for (int i = 0; i < 16; i++) {
                    if (seen_reports[i] == d[0]) return;
                    if (seen_reports[i] == 0) { seen_reports[i] = d[0]; break; }
                }
                ESP_LOGI(TAG, "HID report 0x%02X len=%d first=%02X %02X %02X %02X",
                         d[0], len, d[0], len > 1 ? d[1] : 0,
                         len > 2 ? d[2] : 0, len > 3 ? d[3] : 0);
            }
            break;
        }
        default:
            break;
    }
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
            ESP_LOGW(TAG, "long press: clearing NVS");
            nvs_handle_t h;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_all(h);
                nvs_commit(h);
                nvs_close(h);
            }
            // also clear classic-BT bonded devices
            int n = esp_bt_gap_get_bond_device_num();
            if (n > 0) {
                esp_bd_addr_t list[n];
                esp_bt_gap_get_bond_device_list(&n, list);
                for (int i = 0; i < n; i++) esp_bt_gap_remove_bond_device(list[i]);
            }
            led_blink_fast(8);
            long_fired = true;
        }
        if (last == 0 && now == 1 && !long_fired) {
            ESP_LOGI(TAG, "button: manual wake");
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
    };
    gpio_config(&led_cfg);
    led_off();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    if (config_load(&g_config) == ESP_OK) {
        ESP_LOGI(TAG, "loaded config from NVS");
    } else {
        ESP_LOGI(TAG, "no NVS config; using compile-time secrets.h");
        config_apply_defaults_from_secrets(&g_config);
        config_save(&g_config);
    }
    led_blink_fast(3);

    // Override BT MAC so we impersonate the source Joy-Con during BLE advertising.
    // On classic ESP32: BT MAC = base MAC + 2.
    uint8_t base[6];
    memcpy(base, g_config.source_joycon_mac, 6);
    base[5] -= 2;
    esp_base_mac_addr_set(base);

    esp_timer_create_args_t timer_args = {.callback = stop_advertising, .name = "burst_stop"};
    esp_timer_create(&timer_args, &burst_stop_timer);

    // --- Bluedroid dual-mode init ---
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_LOGI(TAG, "BT MAC: %s", esp_bt_dev_get_address() ?
             "(set)" : "?");
    const uint8_t *bt_addr = esp_bt_dev_get_address();
    if (bt_addr) {
        ESP_LOGI(TAG, "source MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 bt_addr[0], bt_addr[1], bt_addr[2], bt_addr[3], bt_addr[4], bt_addr[5]);
    }
    ESP_LOGI(TAG, "target: %02X:%02X:%02X:%02X:%02X:%02X  PID=0x%04X  state=0x%02X",
             g_config.target_switch_mac[0], g_config.target_switch_mac[1],
             g_config.target_switch_mac[2], g_config.target_switch_mac[3],
             g_config.target_switch_mac[4], g_config.target_switch_mac[5],
             g_config.source_joycon_pid, g_config.state_flag);

    // Register callbacks
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_event_handler));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_event_handler));
    ESP_ERROR_CHECK(esp_bt_hid_host_init());
    ESP_ERROR_CHECK(esp_bt_hid_host_register_callback(hid_host_event_handler));

    // Make ESP32 discoverable + connectable so the Pro Controller can talk.
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    esp_bt_gap_set_device_name("sw2_wake_bridge");

    // Use SSP JUST_WORKS for Pro Controller pairing.
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(iocap));

    // Start scanning for Pro Controller
    ESP_LOGI(TAG, "starting inquiry for Pro Controller...");
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

    xTaskCreate(button_task, "btn", 2048, NULL, 5, NULL);
}
