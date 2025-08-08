import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

usb_ns = cg.global_ns.namespace("usb_hid_keyboard")
UsbHidKeyboardManager = usb_ns.class_("UsbHidKeyboardManager", cg.Component)
UsbHidKeyboardBinarySensor = usb_ns.class_("UsbHidKeyboardBinarySensor", binary_sensor.BinarySensor, cg.Component)

CONF_MANAGER_ID = "usb_hid_keyboard_id"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(UsbHidKeyboardBinarySensor).extend({
    cv.GenerateID(): cv.declare_id(UsbHidKeyboardBinarySensor),
    cv.Required(CONF_MANAGER_ID): cv.use_id(UsbHidKeyboardManager),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    mgr = await cg.get_variable(config[CONF_MANAGER_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)
    cg.add(mgr.register_binary_sensor(var))
