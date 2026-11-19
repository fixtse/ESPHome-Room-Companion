import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_I2C_ID

DEPENDENCIES = ["i2c"]
TouchBus = cg.esphome_ns.namespace("onx_touch_bus").class_("TouchBus", cg.Component, i2c.I2CBus)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TouchBus),
        cv.Required(CONF_I2C_ID): cv.use_id(i2c.InternalI2CBus),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.add_variable(var.set_bus, config[CONF_I2C_ID])
