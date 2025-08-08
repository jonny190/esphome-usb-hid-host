#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/core/log.h"
#include "binary_sensor/usb_hid_keyboard_binary_sensor.h"

static const char *const TAG = "usb_hid_keyboard";

namespace esphome {
namespace usb_hid_keyboard {

static UsbHidKeyboardManager *s_instance = nullptr;

UsbHidKeyboardManager *UsbHidKeyboardManager::instance() { return s_instance; }

void UsbHidKeyboardManager::setup() {
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  s_instance = this;
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  poll_usb_();

  while (!key_queue_.empty()) {
    auto k = key_queue_.front();
    key_queue_.pop();

    if (last_key_sensor_ != nullptr) last_key_sensor_->publish_state(k);
    ESP_LOGV(TAG, "Key: %s", k.c_str());

    // Pulse all registered binary sensors
    for (auto *bs : binary_sensors_) {
      if (bs != nullptr) bs->on_key_pulse();
    }
  }
}

void UsbHidKeyboardManager::enqueue_key(const std::string &key) {
  key_queue_.push(key);
}

void UsbHidKeyboardManager::init_usb_host_() {
  // TODO: IDF USB host + HID init here
  ESP_LOGI(TAG, "USB host init (stub)");
}

void UsbHidKeyboardManager::poll_usb_() {
  // TODO: Handle HID reports → decode → enqueue_key("A");
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
