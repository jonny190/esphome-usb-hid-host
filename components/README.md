# USB HID Keyboard Host (ESPHome external component)

Publishes the **last key** pressed from a USB keyboard connected to an ESP32-S3 in Host/OTG mode.

> Status: Skeleton. USB/HID internals are TODO. Compiles once you fill in the missing codegen wiring and IDF host calls.

## ESPHome YAML

```yaml
esphome:
  name: esp32-s3-usbhost
  platformio_options:
    board_build.flash_mode: dio

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

logger:
  level: VERBOSE

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_pass

api:
ota:

usb_host:
  enable_hubs: true
  devices:
    - id: any_keyboard
      vid: 0
      pid: 0

external_components:
  - source:
      type: git
      url: https://github.com/youruser/esphome-usb-hid-host
      ref: main

# This creates a text_sensor using the external platform
text_sensor:
  - platform: usb_hid_keyboard
    name: "USB Keyboard Last Key"
