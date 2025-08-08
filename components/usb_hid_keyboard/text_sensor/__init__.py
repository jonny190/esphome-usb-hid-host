import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

usb_ns = cg.global_ns.namespace("usb_hid_keyboard")
UsbHidKeyboardManager = usb_ns.class_("UsbHidKeyboardManager", cg.Component)

CONF_MANAGER_ID = "usb_hid_keyboard_id"

CONFIG_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor, icon="mdi:keyboard").extend({
    cv.GenerateID(): cv.declare_id(text_sensor.TextSensor),
    cv.Required(CONF_MANAGER_ID): cv.use_id(UsbHidKeyboardManager),
})

async def to_code(config):
    mgr = await cg.get_variable(config[CONF_MANAGER_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)
    cg.add(mgr.set_last_key_sensor(var))
