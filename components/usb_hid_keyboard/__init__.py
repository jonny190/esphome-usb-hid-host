import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

usb_ns = cg.global_ns.namespace("usb_hid_keyboard")
UsbHidKeyboardManager = usb_ns.class_("UsbHidKeyboardManager", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(UsbHidKeyboardManager),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
