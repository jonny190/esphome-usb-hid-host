#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <queue>
#include <string>
#include <vector>
#include <cstdint>

#include "usb/usb_host.h"

namespace esphome {
namespace usb_hid_keyboard {

class UsbHidKeyboardBinarySensor;

class UsbHidKeyboardManager : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_last_key_sensor(text_sensor::TextSensor *sensor) { last_key_sensor_ = sensor; }
  void register_binary_sensor(UsbHidKeyboardBinarySensor *bs) { binary_sensors_.push_back(bs); }

  void enqueue_key(const std::string &key);

 private:
  void init_usb_host_();
  void poll_usb_();

  usb_host_client_handle_t client_{nullptr};
  usb_device_handle_t dev_handle_{nullptr};
  bool host_installed_{false};
  bool device_open_{false};
  bool interface_claimed_{false};

  uint8_t ep_in_addr_{0};
  usb_transfer_t *xfer_in_{nullptr};
  uint16_t max_packet_size_{0};

  text_sensor::TextSensor *last_key_sensor_{nullptr};
  std::queue<std::string> key_queue_;
  std::vector<UsbHidKeyboardBinarySensor *> binary_sensors_;

  bool open_target_device_(uint16_t vid, uint16_t pid, uint8_t addr);
  bool find_keyboard_interface_and_ep_(const usb_config_desc_t *cfg);
  void submit_next_in_();
  void handle_report_(const uint8_t *data, int len);

  static void xfer_cb_(usb_transfer_t *transfer);
};

}  // namespace usb_hid_keyboard
}  // namespace esphome
