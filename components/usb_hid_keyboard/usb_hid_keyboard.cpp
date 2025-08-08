#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/components/usb_hid_keyboard/binary_sensor/usb_hid_keyboard_binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"  // esphome::millis()

static const char *const TAG = "usb_hid_keyboard";

// Your keyboard from logs: vid 0x1532 pid 0x023F
#define TARGET_VID 0x1532
#define TARGET_PID 0x023F

// HID constants
#define USB_CLASS_HID      0x03
#define HID_SUBCLASS_BOOT  0x01
#define HID_PROTO_KEYBOARD 0x01

namespace esphome {
namespace usb_hid_keyboard {

static UsbHidKeyboardManager *g_mgr = nullptr;

void UsbHidKeyboardManager::setup() {
  g_mgr = this;
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  // Service host client events (nonblocking)
  if (client_) {
    usb_host_client_handle_events(client_, 0);  // 0 = no wait
  }

  // Publish queued keys
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
  // 1) Install host
  usb_host_config_t host_cfg = {
      .intr_flags = ESP_INTR_FLAG_LEVEL1,  // safe default
  };
  if (usb_host_install(&host_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_install failed");
    return;
  }
  host_installed_ = true;

  // 2) Register a client (us)
  usb_host_client_config_t client_cfg = {
      .is_synchronous = false,
      .max_num_event_msg = 10,
      .async = {.callback = [](const usb_host_client_event_msg_t *event_msg, void *) {
        // Basic attach/detach events
        switch (event_msg->event) {
          case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "USB: NEW_DEV");
            // Nothing to do here; we'll open in poll by scanning
            break;
          case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "USB: DEV_GONE");
            // Device disappeared
            break;
          default:
            break;
        }
      },
      .arg = nullptr},
  };

  if (usb_host_client_register(&client_cfg, &client_) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_client_register failed");
    return;
  }

  // 3) Try to open our target device now (if already present)
  open_target_device_(TARGET_VID, TARGET_PID);
}

bool UsbHidKeyboardManager::open_target_device_(uint16_t vid, uint16_t pid) {
  if (device_open_) return true;

  // Enumerate known devices, try open the first matching VID/PID
  usb_host_device_handle_t devs[8];
  int num = 0;
  if (usb_host_device_get_all(devs, 8, &num) != ESP_OK) {
    ESP_LOGW(TAG, "device_get_all failed");
    return false;
  }

  for (int i = 0; i < num; i++) {
    usb_device_handle_t dev;
    if (usb_host_device_open(client_, devs[i].dev_addr, &dev) != ESP_OK) continue;

    const usb_device_desc_t *dd = nullptr;
    usb_host_device_info_t info;
    if (usb_host_device_get_info(dev, &info) == ESP_OK) dd = info.dev_desc;

    if (dd && dd->idVendor == vid && dd->idProduct == pid) {
      ESP_LOGI(TAG, "Opened target device %04X:%04X", vid, pid);
      dev_handle_ = dev;
      device_open_ = true;

      // Get active config
      const usb_config_desc_t *cfg = nullptr;
      if (usb_host_get_active_config_descriptor(dev_handle_, &cfg) != ESP_OK || !cfg) {
        ESP_LOGE(TAG, "get_active_config_descriptor failed");
        return false;
      }

      if (!find_keyboard_interface_and_ep_(cfg)) {
        ESP_LOGE(TAG, "No HID keyboard interface found");
        return false;
      }

      // Allocate first IN transfer and kick it off
      submit_next_in_();
      return true;
    }

    // Not our target; close it
    usb_host_device_close(client_, dev);
  }

  // Not found yet
  return false;
}

bool UsbHidKeyboardManager::find_keyboard_interface_and_ep_(const usb_config_desc_t *cfg) {
  // Walk config->interfaces->endpoints to find HID keyboard + IN interrupt endpoint
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
      if (ifd->bInterfaceClass == USB_CLASS_HID &&
          ifd->bInterfaceSubClass == HID_SUBCLASS_BOOT &&
          ifd->bInterfaceProtocol == HID_PROTO_KEYBOARD) {
        // claim it
        if (usb_host_interface_claim(dev_handle_, 0 /*cfg idx*/, current_iface, 0 /*alt setting*/) == ESP_OK) {
          interface_claimed_ = true;
          ESP_LOGI(TAG, "Claimed HID keyboard interface %u", current_iface);
        } else {
          ESP_LOGE(TAG, "interface_claim failed");
        }
      }
    } else if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const usb_endpoint_desc_t *ep = (const usb_endpoint_desc_t *)p;
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

// static
void UsbHidKeyboardManager::xfer_cb_(usb_transfer_t *transfer) {
  auto *self = static_cast<UsbHidKeyboardManager *>(transfer->context);
  if (!self) return;

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    self->handle_report_(transfer->data_buffer, transfer->actual_num_bytes);
  } else {
    ESP_LOGV(TAG, "IN xfer status=%d", (int)transfer->status);
  }

  // Re-submit for the next report
  self->submit_next_in_();
}

static char map_hid_usage_to_char(uint8_t usage, bool shifted) {
  // A..Z
  if (usage >= 0x04 && usage <= 0x1D) {
    char c = 'a' + (usage - 0x04);
    return shifted ? (char)('A' + (usage - 0x04)) : c;
  }
  // 1..0 (HID 0x1E..0x27)
  if (usage >= 0x1E && usage <= 0x27) {
    const char *base = shifted ? "!@#$%^&*()" : "1234567890";
    return base[usage - 0x1E];
  }
  if (usage == 0x2C) return ' '; // SPACE
  return 0;
}

void UsbHidKeyboardManager::handle_report_(const uint8_t *data, int len) {
  // Boot keyboard report: 8 bytes: [mods][reserved][6 keys...]
  if (len < 8 || !data) return;
  uint8_t mods = data[0];
  bool shifted = (mods & 0x22) || (mods & 0x02) || (mods & 0x20); // either L/R SHIFT bits (0x02,0x20)

  // Emit first nonzero key in the 6 slots (very naive)
  for (int i = 2; i < 8; i++) {
    uint8_t usage = data[i];
    if (!usage) continue;

    if (usage == 0x28) { enqueue_key("ENTER"); return; }  // Enter
    char ch = map_hid_usage_to_char(usage, shifted);
    if (ch) {
      std::string s(1, ch);
      enqueue_key(s);
      return;
    }
    // Otherwise, publish hex code e.g. "0x2A" for Backspace etc.
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", usage);
    enqueue_key(buf);
    return;
  }
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
