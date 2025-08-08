#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace usb_hid_keyboard {

class UsbHidKeyboardBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override {}
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Called by manager when any key arrives
  void on_key_pulse();

 protected:
  bool pending_pulse_{false};
  uint32_t pulse_start_ms_{0};
  uint32_t pulse_len_ms_{50};  // default 50ms ON
};

}  // namespace usb_hid_keyboard
}  // namespace esphome
