#include "esphome/components/usb_hid_keyboard/usb_hid_keyboard.h"
#include "esphome/components/usb_hid_keyboard/binary_sensor/usb_hid_keyboard_binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "usb/usb_helpers.h"
#include "usb/usb_types_ch9.h"

static const char *const TAG = "usb_hid_keyboard";

// Set to 0x0000 to accept any keyboard-like HID device
#define TARGET_VID 0x0000
#define TARGET_PID 0x0000

#define USB_CLASS_HID      0x03

namespace esphome {
namespace usb_hid_keyboard {

void UsbHidKeyboardManager::setup() {
  ESP_LOGI(TAG, "Setting up USB HID Keyboard Manager");
  init_usb_host_();
}

void UsbHidKeyboardManager::loop() {
  // Service the library and the client (non-blocking)
  uint32_t flags = 0;
  (void) usb_host_lib_handle_events(0, &flags);
  if (client_) (void) usb_host_client_handle_events(client_, 0);

  // Optional: probe already-enumerated devices if we missed NEW_DEV
  static uint32_t last_probe_ms = 0;
  if (!device_open_ && (millis() - last_probe_ms) > 2000) {
    last_probe_ms = millis();
    uint8_t addrs[8] = {0};
    int n = 0;
    if (usb_host_device_addr_list_fill(8, addrs, &n) == ESP_OK) {
      for (int i = 0; i < n && !device_open_; i++) {
        ESP_LOGD(TAG, "Probing existing addr %u", addrs[i]);
        (void) open_target_device_(TARGET_VID, TARGET_PID, addrs[i]);
      }
    }
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
  usb_host_config_t host_cfg = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
  if (usb_host_install(&host_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_install failed");
    return;
  }
  host_installed_ = true;

  // Ensure VBUS/root port is ON
  (void) usb_host_lib_set_root_port_power(true);

  usb_host_client_config_t client_cfg = {
      .is_synchronous = false,
      .max_num_event_msg = 10,
      .async = {
          .client_event_callback = [](const usb_host_client_event_msg_t *event_msg, void *arg) {
            auto *self = static_cast<UsbHidKeyboardManager *>(arg);
            if (!self) return;

            switch (event_msg->event) {
              case USB_HOST_CLIENT_EVENT_NEW_DEV: {
                uint8_t addr = event_msg->new_dev.address;
                ESP_LOGI(TAG, "NEW_DEV addr=%u", addr);
                self->open_target_device_(TARGET_VID, TARGET_PID, addr);
                break;
              }
              case USB_HOST_CLIENT_EVENT_DEV_GONE: {
                ESP_LOGI(TAG, "DEV_GONE");
                if (self->xfer_in_) {
                  usb_host_transfer_free(self->xfer_in_);
                  self->xfer_in_ = nullptr;
                }
                if (self->device_open_ && self->dev_handle_) {
                  usb_host_device_close(self->client_, self->dev_handle_);
                }
                self->dev_handle_ = nullptr;
                self->device_open_ = false;
                self->interface_claimed_ = false;
                self->claimed_iface_ = 0xFF;
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
  ESP_LOGI(TAG, "USB host client registered");
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

  // If filters are non-zero, enforce them; else accept any device
  if ((vid && dd->idVendor != vid) || (pid && dd->idProduct != pid)) {
    ESP_LOGI(TAG, "addr %u is %04X:%04X (filtered out)", addr, dd->idVendor, dd->idProduct);
    usb_host_device_close(client_, dev);
    return false;
  }

  ESP_LOGI(TAG, "Opened %04X:%04X at addr %u", dd->idVendor, dd->idProduct, addr);
  dev_handle_ = dev;
  device_open_ = true;

  const usb_config_desc_t *cfg = nullptr;
  if (usb_host_get_active_config_descriptor(dev_handle_, &cfg) != ESP_OK || !cfg) {
    ESP_LOGE(TAG, "active config desc failed");
    return false;
  }

  // Dump config once for visibility
  {
    const uint8_t *p = (const uint8_t *)cfg, *end = p + cfg->wTotalLength;
    ESP_LOGI(TAG, "Active config total len=%u", (unsigned)cfg->wTotalLength);
    while (p < end) {
      auto *hdr = (const usb_standard_desc_t *)p;
      if (hdr->bLength == 0) break;
      if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
        auto *ifd = (const usb_intf_desc_t *)p;
        ESP_LOGI(TAG, "IFACE %u: class=%02X sub=%02X proto=%02X alt=%u",
                 ifd->bInterfaceNumber, ifd->bInterfaceClass, ifd->bInterfaceSubClass,
                 ifd->bInterfaceProtocol, ifd->bAlternateSetting);
      } else if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
        auto *ep = (const usb_ep_desc_t *)p;
        ESP_LOGI(TAG, "  EP 0x%02X attr=%02X mps=%u",
                 ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize);
      }
      p += hdr->bLength;
    }
  }

  if (!find_keyboard_interface_and_ep_(cfg)) {
    ESP_LOGE(TAG, "No HID keyboard-like interface found");
    return false;
  }

  // Force Boot protocol + Idle 0 so we get 8-byte boot reports even for gaming readers
  (void) hid_set_boot_protocol_(claimed_iface_);
  (void) hid_set_idle_(claimed_iface_, 0, 0);

  submit_next_in_();
  return true;
}

bool UsbHidKeyboardManager::find_keyboard_interface_and_ep_(const usb_config_desc_t *cfg) {
  interface_claimed_ = false;
  claimed_iface_ = 0xFF;
  ep_in_addr_ = 0;
  max_packet_size_ = 0;

  const uint8_t *p = (const uint8_t *)cfg;
  const uint8_t *end = p + cfg->wTotalLength;
  uint8_t current_iface = 0xFF;

  while (p < end) {
    const usb_standard_desc_t *hdr = (const usb_standard_desc_t *)p;
    if (hdr->bLength == 0) break;

    if (hdr->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const usb_intf_desc_t *ifd = (const usb_intf_desc_t *)p;
      current_iface = ifd->bInterfaceNumber;
      ESP_LOGD(TAG, "IFACE #%u class=%02X sub=%02X proto=%02X",
               ifd->bInterfaceNumber, ifd->bInterfaceClass, ifd->bInterfaceSubClass, ifd->bInterfaceProtocol);

      // Accept ANY HID interface (keyboard-wedge devices are often not boot proto)
      if (ifd->bInterfaceClass == USB_CLASS_HID) {
        esp_err_t err = usb_host_interface_claim(client_, dev_handle_, current_iface, ifd->bAlternateSetting);
        if (err == ESP_OK) {
          interface_claimed_ = true;
          claimed_iface_ = current_iface;
          ESP_LOGI(TAG, "Claimed HID interface %u (sub=%02X proto=%02X alt=%u)",
                   current_iface, ifd->bInterfaceSubClass, ifd->bInterfaceProtocol, ifd->bAlternateSetting);
        } else {
          ESP_LOGE(TAG, "interface_claim failed for iface %u err=%d", current_iface, (int)err);
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

  ESP_LOGW(TAG, "No suitable HID IN interrupt endpoint found");
  return false;
}

bool UsbHidKeyboardManager::hid_set_boot_protocol_(uint8_t iface) {
  if (!dev_handle_ || iface == 0xFF) return false;
  usb_transfer_t *xfer = nullptr;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &xfer) != ESP_OK || !xfer) return false;

  auto *setup = reinterpret_cast<usb_setup_packet_t *>(xfer->data_buffer);
  setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                         USB_BM_REQUEST_TYPE_TYPE_CLASS |
                         USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
  setup->bRequest = 0x0B;     // SET_PROTOCOL
  setup->wValue = 0x0000;     // BOOT
  setup->wIndex = iface;
  setup->wLength = 0;

  xfer->device_handle = dev_handle_;
  xfer->bEndpointAddress = 0;
  xfer->num_bytes = sizeof(usb_setup_packet_t);
  xfer->callback = nullptr;
  xfer->context = nullptr;

  esp_err_t err = usb_host_transfer_submit_control(client_, xfer);
  bool ok = (err == ESP_OK) && (xfer->status == USB_TRANSFER_STATUS_COMPLETED);
  usb_host_transfer_free(xfer);
  ESP_LOGI(TAG, "SET_PROTOCOL BOOT iface=%u -> %s", iface, ok ? "OK" : "FAIL");
  return ok;
}

bool UsbHidKeyboardManager::hid_set_idle_(uint8_t iface, uint8_t duration, uint8_t report_id) {
  if (!dev_handle_ || iface == 0xFF) return false;
  usb_transfer_t *xfer = nullptr;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &xfer) != ESP_OK || !xfer) return false;

  auto *setup = reinterpret_cast<usb_setup_packet_t *>(xfer->data_buffer);
  setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                         USB_BM_REQUEST_TYPE_TYPE_CLASS |
                         USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
  setup->bRequest = 0x0A;                 // SET_IDLE
  setup->wValue = (duration << 8) | report_id;
  setup->wIndex = iface;
  setup->wLength = 0;

  xfer->device_handle = dev_handle_;
  xfer->bEndpointAddress = 0;
  xfer->num_bytes = sizeof(usb_setup_packet_t);
  xfer->callback = nullptr;
  xfer->context = nullptr;

  esp_err_t err = usb_host_transfer_submit_control(client_, xfer);
  bool ok = (err == ESP_OK) && (xfer->status == USB_TRANSFER_STATUS_COMPLETED);
  usb_host_transfer_free(xfer);
  ESP_LOGI(TAG, "SET_IDLE iface=%u -> %s", iface, ok ? "OK" : "FAIL");
  return ok;
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
  ESP_LOGI(TAG, "Submitting IN xfer size=%u on ep=0x%02X", (unsigned) xfer_in_->num_bytes, ep_in_addr_);
  esp_err_t err = usb_host_transfer_submit(xfer_in_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "transfer_submit failed: %d", (int)err);
  }
}

void UsbHidKeyboardManager::xfer_cb_(usb_transfer_t *transfer) {
  auto *self = static_cast<UsbHidKeyboardManager *>(transfer->context);
  if (!self) return;

  ESP_LOGV(TAG, "IN xfer status=%d len=%d", (int)transfer->status, (int)transfer->actual_num_bytes);
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    self->handle_report_(transfer->data_buffer, transfer->actual_num_bytes);
  }
  self->submit_next_in_();
}

// Basic usage->char for boot reports (fallback if device is in boot)
// For readers that send ASCII in report data, we’ll also push raw ASCII below.
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
  if (!data || len <= 0) return;

  // First log the raw report for debugging Paxton/Keychron formats
  ESP_LOGI(TAG, "Raw report len=%d", len);
  for (int i = 0; i < len; i++) {
    ESP_LOGI(TAG, "  [%02d] 0x%02X", i, data[i]);
  }

  // If this looks like a boot keyboard report (8 bytes)
  if (len >= 8) {
    bool shifted = (data[0] & 0x22) != 0;  // LSHIFT/RSHIFT
    for (int i = 2; i < 8; i++) {
      uint8_t usage = data[i];
      if (!usage) continue;
      if (usage == 0x28) { enqueue_key("ENTER"); return; }
      char ch = map_hid_usage_to_char(usage, shifted);
      if (ch) { std::string s(1, ch); enqueue_key(s); return; }
    }
  }

  // Many keyboard-wedge readers (Paxton) put ASCII bytes directly in the report.
  // Push any printable ASCII bytes as characters.
  std::string ascii;
  for (int i = 0; i < len; i++) {
    if (data[i] >= 0x20 && data[i] <= 0x7E) ascii.push_back((char)data[i]);
    if (data[i] == '\r' || data[i] == '\n') {
      if (!ascii.empty()) { enqueue_key(ascii); ascii.clear(); }
    }
  }
  if (!ascii.empty()) enqueue_key(ascii);
}

}  // namespace usb_hid_keyboard
}  // namespace esphome
