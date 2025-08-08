#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/components/usb_hid_keyboard/binary_sensor/usb_hid_keyboard_binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"  // esphome::millis()

static const char *const TAG = "usb_hid_keyboard";

namespace esphome {
namespace usb_hid_keyboard {

void UsbHidKeyboardManager::setup() {
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  // Pump USB host state (reports → enqueue_key)
  poll_usb_();

  // Publish any queued keys to the text sensor and pulse binary sensors
  while (!key_queue_.empty()) {
    auto k = key_queue_.front();
    key_queue_.pop();

    if (last_key_sensor_ != nullptr) {
      last_key_sensor_->publish_state(k.c_str());
    }
    for (auto *bs : binary_sensors_) {
      if (bs != nullptr) bs->on_key_pulse();
    }
  }
}

void UsbHidKeyboardManager::enqueue_key(const std::string &key) {
  key_queue_.push(key);
}

void UsbHidKeyboardManager::init_usb_host_() {
  // TODO: ESP-IDF USB host + HID init
  ESP_LOGI(TAG, "USB host init (stub)");
}

void UsbHidKeyboardManager::poll_usb_() {
  // TODO: Pump host client events / decode HID reports → enqueue_key(...)
  // Fake a key every 2s for plumbing test:
  static uint32_t last = 0;
  if (esphome::millis() - last > 2000) {
    last = esphome::millis();
    enqueue_key("TEST");
  }
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
