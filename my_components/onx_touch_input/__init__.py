"""Touch-to-knob input for rectangular panels (see onx_touch_input.h)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import touchscreen
from esphome.const import CONF_ID

AUTO_LOAD = ["onx_button"]
DEPENDENCIES = ["touchscreen"]

ns = cg.esphome_ns.namespace("onx_touch_input")
TouchInput = ns.class_("TouchInput", cg.Component)
BufferedButton = cg.esphome_ns.namespace("onx_button").class_("BufferedButton")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TouchInput),
        cv.GenerateID("touchscreen_id"): cv.use_id(touchscreen.Touchscreen),
        cv.Required("button_id"): cv.use_id(BufferedButton),
        cv.Required("width"): cv.positive_int,
        cv.Required("strip_top"): cv.positive_int,
        cv.Required("strip_spacing"): cv.positive_int,
        cv.Required("step"): cv.positive_int,
        cv.Optional("hold", default="350ms"): cv.positive_time_period_milliseconds,
        cv.Optional("passthrough", default=[]): cv.ensure_list(
            cv.Schema({cv.Required("x0"): cv.int_, cv.Required("y0"): cv.int_,
                       cv.Required("x1"): cv.int_, cv.Required("y1"): cv.int_})
        ),
        cv.Optional("page_swipe"): cv.returning_lambda,
        cv.Optional("back_enabled"): cv.returning_lambda,
        cv.Optional("native_touch"): cv.returning_lambda,
        # True: back needs a swipe from the left edge (horizontal swipes elsewhere turn).
        cv.Optional("edge_back"): cv.returning_lambda,
        # Swipe down from the top edge (e.g. open the notification centre).
        cv.Optional("pull_down_enabled"): cv.returning_lambda,
        cv.Optional("on_pull_down"): automation.validate_automation({}),
        cv.Optional("on_back"): automation.validate_automation({}),
        cv.Optional("enabled"): cv.returning_lambda,
        cv.Optional("on_turn"): automation.validate_automation({}),
        cv.Optional("on_strip_tap"): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_touchscreen(await cg.get_variable(config["touchscreen_id"])))
    cg.add(var.set_button(await cg.get_variable(config["button_id"])))
    cg.add(var.set_geometry(config["width"], config["strip_top"], config["strip_spacing"], config["step"]))
    cg.add(var.set_hold_ms(config["hold"].total_milliseconds))
    for region in config["passthrough"]:
        cg.add(var.add_passthrough(region["x0"], region["y0"], region["x1"], region["y1"]))
    if "enabled" in config:
        lam = await cg.process_lambda(config["enabled"], [], return_type=cg.bool_)
        cg.add(var.set_enabled_lambda(lam))
    for key in ("back_enabled", "native_touch", "page_swipe", "edge_back", "pull_down_enabled"):
        if key in config:
            lam = await cg.process_lambda(config[key], [], return_type=cg.bool_)
            cg.add(getattr(var, "set_" + key)(lam))
    for conf in config.get("on_back", []):
        await automation.build_automation(var.get_back_trigger(), [], conf)
    for conf in config.get("on_pull_down", []):
        await automation.build_automation(var.get_pull_down_trigger(), [], conf)
    for conf in config.get("on_turn", []):
        await automation.build_automation(var.get_turn_trigger(), [(cg.int_, "direction")], conf)
    for conf in config.get("on_strip_tap", []):
        await automation.build_automation(var.get_strip_trigger(), [(cg.int_, "offset")], conf)
