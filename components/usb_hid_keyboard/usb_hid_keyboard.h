#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <queue>
#include <string>
#include <vector>

namespace esphome {
namespace usb_hid_keyboard {

// Forward-declare to avoid circular include
class UsbHidKeyboardBinarySensor;

class UsbHidKeyboardManager : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Wiring from platforms
  void set_last_key_sensor(text_sensor::TextSensor *sensor) { last_key_sensor_ = sensor; }
  void register_binary_sensor(UsbHidKeyboardBinarySensor *bs) { binary_sensors_.push_back(bs); }

  // Called by HID callback / poller when a key is decoded
  void enqueue_key(const std::string &key);

 private:
  void init_usb_host_();
  void poll_usb_();

  text_sensor::TextSensor *last_key_sensor_{nullptr};
  std::queue<std::string> key_queue_;
  std::vector<UsbHidKeyboardBinarySensor *> binary_sensors_;
};

}  // namespace usb_hid_keyboard
}  // namespace esphome
