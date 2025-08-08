#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <queue>
#include <string>

namespace esphome {
namespace usb_hid_keyboard {

class UsbHidKeyboardManager : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Called by text_sensor platform to attach itself as an output
  void set_last_key_sensor(text_sensor::TextSensor *sensor) { last_key_sensor_ = sensor; }

  // Hook for the USB ISR/task to enqueue decoded key strings
  void enqueue_key(const std::string &key);

  // Singleton-ish access if needed
  static UsbHidKeyboardManager *instance();

 private:
  void init_usb_host_();
  void poll_usb_();

  text_sensor::TextSensor *last_key_sensor_{nullptr};
  std::queue<std::string> key_queue_;

  // TODO: Add your USB host handles / HID driver state here
  // e.g. usb_host_client_handle_t client_handle_;
};

}  // namespace usb_hid_keyboard
}  // namespace esphome
