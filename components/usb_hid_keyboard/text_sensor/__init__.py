import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

usb_ns = cg.global_ns.namespace('esphome').namespace('usb_hid_keyboard')
UsbHidKeyboardManager = usb_ns.class_('UsbHidKeyboardManager', cg.Component)
UsbHidKeyboardBinarySensor = usb_ns.class_('UsbHidKeyboardBinarySensor', binary_sensor.BinarySensor, cg.Component)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(UsbHidKeyboardBinarySensor).extend(
    cv.COMPONENT_SCHEMA
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)

    # Create/get the single manager instance for this node
    mgr = cg.new_Pvariable(UsbHidKeyboardManager)
    await cg.register_component(mgr, {})
    cg.add(mgr.register_binary_sensor(var))
