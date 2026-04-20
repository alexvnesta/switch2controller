#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"

#ifdef USE_ESP32
#include "esphome/components/esp32_ble/ble.h"
#endif

namespace esphome {
namespace switch2_wake {

using namespace esp32_ble;

class Switch2WakeComponent : public Component, public GAPEventHandler, public Parented<ESP32BLE> {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;
  void send_once();
  void set_burst_ms(uint32_t burst_ms) { this->burst_ms_ = burst_ms; }
  void set_packet_count(uint16_t packet_count) { this->packet_count_ = packet_count; }

 protected:
#ifdef USE_ESP32
  void start_advertising_burst_();
#endif

  bool config_in_flight_{false};
  bool advertising_{false};
  uint32_t burst_ms_{2000};
  uint16_t packet_count_{500};
  esp_ble_adv_params_t adv_params_{};
};

class Switch2WakeBurstButton : public button::Button {
 public:
  void set_parent(Switch2WakeComponent *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  Switch2WakeComponent *parent_{nullptr};
};

}  // namespace switch2_wake
}  // namespace esphome
