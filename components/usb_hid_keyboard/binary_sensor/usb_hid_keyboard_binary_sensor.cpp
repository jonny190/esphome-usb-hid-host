#include "esphome/components/usb_hid_keyboard/binary_sensor/usb_hid_keyboard_binary_sensor.h"
#include "esphome/core/hal.h"  // esphome::millis()

namespace esphome {
namespace usb_hid_keyboard {

void UsbHidKeyboardBinarySensor::on_key_pulse() {
  this->publish_state(true);
  this->pending_pulse_ = true;
  this->pulse_start_ms_ = esphome::millis();
}

void UsbHidKeyboardBinarySensor::loop() {
  if (pending_pulse_) {
    if ((esphome::millis() - pulse_start_ms_) >= pulse_len_ms_) {
      this->publish_state(false);
      pending_pulse_ = false;
    }
  }
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
