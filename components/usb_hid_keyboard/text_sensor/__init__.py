import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

usb_ns = cg.global_ns.namespace('esphome').namespace('usb_hid_keyboard')
UsbHidKeyboardManager = usb_ns.class_('UsbHidKeyboardManager', cg.Component)

CONF_USB_LAST_KEY_ID = "usb_last_key_id"

CONFIG_SCHEMA = text_sensor.text_sensor_schema(
    text_sensor.TextSensor, icon="mdi:keyboard"
).extend({
    cv.GenerateID(): cv.declare_id(text_sensor.TextSensor),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # Ensure the core manager exists or create one
    mgr = cg.add_global(UsbHidKeyboardManager.instance())
    # If instance() pattern isn’t available at codegen, create an object:
    mgr_var = cg.new_Pvariable(cg.global_ns, UsbHidKeyboardManager)
    await cg.register_component(mgr_var, {})
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)

    # Link the sensor to the manager
    cg.add(mgr_var.set_last_key_sensor(var))
