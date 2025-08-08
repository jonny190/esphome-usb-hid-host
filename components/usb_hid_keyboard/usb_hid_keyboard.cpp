#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/core/log.h"

static const char *const TAG = "usb_hid_keyboard";

namespace esphome {
namespace usb_hid_keyboard {

static UsbHidKeyboardManager *s_instance = nullptr;

UsbHidKeyboardManager *UsbHidKeyboardManager::instance() {
  return s_instance;
}

void UsbHidKeyboardManager::setup() {
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  s_instance = this;
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  // TODO: If you run the HID host in a separate task, this can be lightweight.
  poll_usb_();

  // Publish any queued keys
  while (!key_queue_.empty()) {
    auto k = key_queue_.front();
    key_queue_.pop();
    if (last_key_sensor_ != nullptr) {
      last_key_sensor_->publish_state(k);
    }
    ESP_LOGV(TAG, "Key: %s", k.c_str());
  }
}

void UsbHidKeyboardManager::enqueue_key(const std::string &key) {
  key_queue_.push(key);
}

void UsbHidKeyboardManager::init_usb_host_() {
  // TODO: Initialize ESP-IDF USB host (OTG) stack and HID driver
  // - usb_host_install_driver()
  // - create client
  // - register callbacks
  // - enumerate devices and open HID interface with boot keyboard subclass
  //
  // See: ESP-IDF HID host example
  ESP_LOGI(TAG, "USB host init (stub)");
}

void UsbHidKeyboardManager::poll_usb_() {
  // TODO: Pump host client events, HID transfers, parse reports -> enqueue_key()
  // For bring-up, you can simulate:
  // enqueue_key("A"); delay(500); etc.
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
