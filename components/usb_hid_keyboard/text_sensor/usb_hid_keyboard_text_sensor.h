#pragma once
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace usb_hid_keyboard {

class UsbHidKeyboardTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override {}
  void loop() override {}
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace usb_hid_keyboard
}  // namespace esphome
