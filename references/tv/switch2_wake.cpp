#include "switch2_wake.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <esp_mac.h>
#endif

namespace esphome {
namespace switch2_wake {

static const char *const TAG = "switch2_wake";

void Switch2WakeComponent::setup() {
#ifdef USE_ESP32
  static uint8_t k_base_mac[6] = {0x98, 0xE2, 0x55, 0xAE, 0xF7, 0xD6};
  esp_err_t err = esp_base_mac_addr_set(k_base_mac);
  if(err != ESP_OK) {
    ESP_LOGW(TAG, "esp_base_mac_addr_set failed: %d", err);
    ESP_LOGW(TAG, "Exact public address requirement may not be met");
  }

  this->adv_params_ = {
      .adv_int_min = 0x50,  // 50ms default (20 events per second target)
      .adv_int_max = 0x50,
      .adv_type = ADV_TYPE_IND,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };
#endif
}

void Switch2WakeComponent::send_once() {
#ifdef USE_ESP32
  if(global_ble == nullptr || !global_ble->is_active()) {
    ESP_LOGW(TAG, "BLE stack is not active yet");
    return;
  }

  if(this->config_in_flight_ || this->advertising_) {
    ESP_LOGW(TAG, "Previous burst is still in progress");
    return;
  }

  static uint8_t k_adv_payload[31] = {
      0x02, 0x01, 0x06, 0x1B, 0xFF, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7E,
      0x05, 0x66, 0x20, 0x00, 0x01, 0x81, 0x97, 0x1F, 0x99, 0x55, 0xE2,
      0x98, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  esp_err_t err = esp_ble_gap_config_adv_data_raw(k_adv_payload, sizeof(k_adv_payload));
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_config_adv_data_raw failed: %d", err);
    return;
  }
  this->config_in_flight_ = true;
#endif
}

void Switch2WakeComponent::start_advertising_burst_() {
#ifdef USE_ESP32
  if(this->advertising_) return;

  // Advertising interval units are 0.625ms.
  // Use window/packet_count to target roughly N advertising events in the burst window.
  uint32_t interval_ms = this->burst_ms_ / (this->packet_count_ == 0 ? 1 : this->packet_count_);
  if(interval_ms < 20) interval_ms = 20;
  if(interval_ms > 10240) interval_ms = 10240;
  uint16_t interval_units = static_cast<uint16_t>((interval_ms * 1000U) / 625U);
  if(interval_units < 0x20) interval_units = 0x20;
  if(interval_units > 0x4000) interval_units = 0x4000;
  this->adv_params_.adv_int_min = interval_units;
  this->adv_params_.adv_int_max = interval_units;

  esp_err_t err = esp_ble_gap_start_advertising(&this->adv_params_);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %d", err);
    return;
  }

  this->advertising_ = true;
#endif
}

void Switch2WakeBurstButton::press_action() {
  if(this->parent_ != nullptr) {
    this->parent_->send_once();
  }
}

#ifdef USE_ESP32
void Switch2WakeComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if(event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT) {
    this->config_in_flight_ = false;
    if(param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      this->start_advertising_burst_();
    } else {
      ESP_LOGE(TAG, "Raw ADV payload config failed, status=%d", param->adv_data_raw_cmpl.status);
    }
    return;
  }

  if(event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    if(param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      this->set_timeout("switch2_wake_stop_adv", this->burst_ms_, [this]() {
        esp_ble_gap_stop_advertising();
      });
    } else {
      this->advertising_ = false;
      ESP_LOGE(TAG, "BLE adv start failed, status=%d", param->adv_start_cmpl.status);
    }
    return;
  }

  if(event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
    this->advertising_ = false;
  }
}

#endif

}  // namespace switch2_wake
}  // namespace esphome
