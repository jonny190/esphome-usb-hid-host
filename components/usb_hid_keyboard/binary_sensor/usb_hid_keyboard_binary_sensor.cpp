#include "usb_hid_keyboard_binary_sensor.h"
#include "esphhome/core/helpers.h"

namespace esphome {
namespace usb_hid_keyboard {

void UsbHidKeyboardBinarySensor::on_key_pulse() {
  this->publish_state(true);
  this->pending_pulse_ = true;
  this->pulse_start_ms_ = millis();
}

void UsbHidKeyboardBinarySensor::loop() {
  if (pending_pulse_) {
    if ((millis() - pulse_start_ms_) >= pulse_len_ms_) {
      this->publish_state(false);
      pending_pulse_ = false;
    }
  }
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
