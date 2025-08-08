#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/components/usb_hid_keyboard/binary_sensor/usb_hid_keyboard_binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

static const char *const TAG = "usb_hid_keyboard";

#define TARGET_VID 0x1532
#define TARGET_PID 0x023F

#define USB_CLASS_HID      0x03
#define HID_SUBCLASS_BOOT  0x01
#define HID_PROTO_KEYBOARD 0x01

namespace esphome {
namespace usb_hid_keyboard {

void UsbHidKeyboardManager::setup() {
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  // NEW: tick the library
  uint32_t lib_flags = 0;
  (void) usb_host_lib_handle_events(0, &lib_flags);
  
  if (client_) {
    (void) usb_host_client_handle_events(client_, 0);
  }

  while (!key_queue_.empty()) {
    auto k = key_queue_.front();
    key_queue_.pop();
    if (last_key_sensor_) last_key_sensor_->publish_state(k.c_str());
    for (auto *bs : binary_sensors_) if (bs) bs->on_key_pulse();
  }
}

void UsbHidKeyboardManager::enqueue_key(const std::string &key) {
  key_queue_.push(key);
}

void UsbHidKeyboardManager::init_usb_host_() {
  usb_host_config_t host_cfg = {
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  if (usb_host_install(&host_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_install failed");
    return;
  }

    (void) usb_host_lib_set_root_port_power(true);

  host_installed_ = true;

usb_host_client_config_t client_cfg = {
    .is_synchronous = false,
    .max_num_event_msg = 10,
    .async = {
        .client_event_callback = [](const usb_host_client_event_msg_t *event_msg, void *arg) {
          auto *self = static_cast<UsbHidKeyboardManager *>(arg);
          if (!self) return;

          switch (event_msg->event) {
            case USB_HOST_CLIENT_EVENT_NEW_DEV: {
              // Address field is 'address' in your headers
              uint8_t addr = event_msg->new_dev.address;
              self->open_target_device_(TARGET_VID, TARGET_PID, addr);
              break;
            }
            case USB_HOST_CLIENT_EVENT_DEV_GONE: {
              // Tidy up transfers/handles if our device went away
              if (self->xfer_in_) {
                usb_host_transfer_free(self->xfer_in_);
                self->xfer_in_ = nullptr;
              }
              if (self->device_open_ && self->dev_handle_) {
                // (If you claimed interfaces, you could release them here)
                usb_host_device_close(self->client_, self->dev_handle_);
              }
              self->dev_handle_ = nullptr;
              self->device_open_ = false;
              self->interface_claimed_ = false;
              self->ep_in_addr_ = 0;
              self->max_packet_size_ = 0;
              break;
            }
            default:
              break;
          }
        },
        .callback_arg = this,
    },
};

  if (usb_host_client_register(&client_cfg, &client_) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_client_register failed");
    return;
  }
}

bool UsbHidKeyboardManager::open_target_device_(uint16_t vid, uint16_t pid, uint8_t addr) {
  if (device_open_) return true;

  usb_device_handle_t dev{};
  if (usb_host_device_open(client_, addr, &dev) != ESP_OK) {
    ESP_LOGW(TAG, "open addr %u failed", addr);
    return false;
  }

  // Get the cached device descriptor
  const usb_device_desc_t *dd = nullptr;
  if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || !dd) {
    ESP_LOGW(TAG, "get_device_descriptor failed");
    usb_host_device_close(client_, dev);
    return false;
  }

  if (dd->idVendor != vid || dd->idProduct != pid) {
    ESP_LOGI(TAG, "addr %u is %04X:%04X (not our target)", addr, dd->idVendor, dd->idProduct);
    usb_host_device_close(client_, dev);
    return false;
  }

  ESP_LOGI(TAG, "Opened %04X:%04X at addr %u", vid, pid, addr);
  dev_handle_ = dev;
  device_open_ = true;

  const usb_config_desc_t *cfg = nullptr;
  if (usb_host_get_active_config_descriptor(dev_handle_, &cfg) != ESP_OK || !cfg) {
    ESP_LOGE(TAG, "active config desc failed");
    return false;
  }

  if (!find_keyboard_interface_and_ep_(cfg)) {
    ESP_LOGE(TAG, "No HID keyboard interface found");
    return false;
  }

  submit_next_in_();
  return true;
}

bool UsbHidKeyboardManager::find_keyboard_interface_and_ep_(const usb_config_desc_t *cfg) {
  const uint8_t *p = (const uint8_t *)cfg;
  const uint8_t *end = p + cfg->wTotalLength;
  uint8_t current_iface = 0xFF;

  while (p < end) {
    const usb_standard_desc_t *hdr = (const usb_standard_desc_t *)p;
    if (hdr->bLength == 0) break;

    if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const usb_intf_desc_t *ifd = (const usb_intf_desc_t *)p;
      current_iface = ifd->bInterfaceNumber;
      ESP_LOGD(TAG, "IFACE #%u class=%02X subclass=%02X proto=%02X",
               ifd->bInterfaceNumber, ifd->bInterfaceClass, ifd->bInterfaceSubClass, ifd->bInterfaceProtocol);

      if (ifd->bInterfaceClass == USB_CLASS_HID) {
        if (usb_host_interface_claim(client_, dev_handle_, current_iface, 0) == ESP_OK) {
          interface_claimed_ = true;
          ESP_LOGI(TAG, "Claimed HID interface %u (class=%02X sub=%02X proto=%02X)",
                  current_iface, ifd->bInterfaceClass, ifd->bInterfaceSubClass, ifd->bInterfaceProtocol);
        } else {
          ESP_LOGE(TAG, "interface_claim failed for iface %u", current_iface);
        }
}
    } else if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const usb_ep_desc_t *ep = (const usb_ep_desc_t *)p;
      bool is_in = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      bool is_int = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
      if (interface_claimed_ && is_in && is_int) {
        ep_in_addr_ = ep->bEndpointAddress;
        max_packet_size_ = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "Found IN interrupt endpoint 0x%02X, mps=%u", ep_in_addr_, max_packet_size_);
        return true;
      }
    }

    p += hdr->bLength;
  }

  return false;
}

void UsbHidKeyboardManager::submit_next_in_() {
  if (!dev_handle_ || ep_in_addr_ == 0) return;

  if (!xfer_in_) {
    if (usb_host_transfer_alloc(max_packet_size_, 0, &xfer_in_) != ESP_OK || !xfer_in_) {
      ESP_LOGE(TAG, "transfer_alloc failed");
      return;
    }
    xfer_in_->callback = &UsbHidKeyboardManager::xfer_cb_;
    xfer_in_->context = this;
    xfer_in_->device_handle = dev_handle_;
    xfer_in_->bEndpointAddress = ep_in_addr_;
  }

  xfer_in_->num_bytes = max_packet_size_;
  esp_err_t err = usb_host_transfer_submit(xfer_in_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "transfer_submit failed: %d", (int)err);
  }
}

void UsbHidKeyboardManager::xfer_cb_(usb_transfer_t *transfer) {
  auto *self = static_cast<UsbHidKeyboardManager *>(transfer->context);
  if (!self) return;

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    self->handle_report_(transfer->data_buffer, transfer->actual_num_bytes);
  } else {
    ESP_LOGV(TAG, "IN xfer status=%d", (int)transfer->status);
  }

  self->submit_next_in_();
}

static char map_hid_usage_to_char(uint8_t usage, bool shifted) {
  if (usage >= 0x04 && usage <= 0x1D) {
    char c = 'a' + (usage - 0x04);
    return shifted ? (char)('A' + (usage - 0x04)) : c;
  }
  if (usage >= 0x1E && usage <= 0x27) {
    const char *base = shifted ? "!@#$%^&*()" : "1234567890";
    return base[usage - 0x1E];
  }
  if (usage == 0x2C) return ' ';
  return 0;
}

void UsbHidKeyboardManager::handle_report_(const uint8_t *data, int len) {
  if (len < 8 || !data) return;
  bool shifted = (data[0] & 0x22) != 0;  // LSHIFT/RSHIFT

  for (int i = 2; i < 8; i++) {
    uint8_t usage = data[i];
    if (!usage) continue;

    if (usage == 0x28) { enqueue_key("ENTER"); return; }
    char ch = map_hid_usage_to_char(usage, shifted);
    if (ch) { std::string s(1, ch); enqueue_key(s); return; }

    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", usage);
    enqueue_key(buf);
    return;
  }
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
