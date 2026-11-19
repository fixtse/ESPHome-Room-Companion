"""Room Companion panel extras: dashboard stream settings and display ownership."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import switch, text, touchscreen
from esphome.const import CONF_ID

AUTO_LOAD = ["json"]
DEPENDENCIES = ["lvgl", "wifi", "remote_webview"]

ns = cg.esphome_ns.namespace("room_companion")
RoomPanel = ns.class_("RoomPanel", cg.Component)
Overlay = ns.enum("Overlay")
RemoteWebView = cg.esphome_ns.namespace("remote_webview").class_("RemoteWebView", cg.Component)
LvglComponent = cg.esphome_ns.namespace("lvgl").class_("LvglComponent", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RoomPanel),
        cv.GenerateID("lvgl_id"): cv.use_id(LvglComponent),
        cv.GenerateID("stream_id"): cv.use_id(RemoteWebView),
        cv.GenerateID("touchscreen_id"): cv.use_id(touchscreen.Touchscreen),
        cv.Optional("url_text"): cv.use_id(text.Text),
        cv.Optional("dashboard_switch"): cv.use_id(switch.Switch),
        cv.Optional("default_server", default=""): cv.string,
        cv.Optional("default_url", default=""): cv.string,
        cv.Optional("on_owner"): automation.validate_automation({}),
        cv.Optional("on_message"): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_lvgl(await cg.get_variable(config["lvgl_id"])))
    cg.add(var.set_stream(await cg.get_variable(config["stream_id"])))
    cg.add(var.set_touchscreen(await cg.get_variable(config["touchscreen_id"])))
    if "url_text" in config:
        cg.add(var.set_url_text(await cg.get_variable(config["url_text"])))
    if "dashboard_switch" in config:
        cg.add(var.set_dashboard_switch(await cg.get_variable(config["dashboard_switch"])))
    cg.add(var.set_defaults(config["default_server"], config["default_url"]))
    for conf in config.get("on_owner", []):
        await automation.build_automation(var.get_owner_trigger(), [(cg.bool_, "dashboard")], conf)
    for conf in config.get("on_message", []):
        await automation.build_automation(
            var.get_message_trigger(),
            [(cg.std_string, "message"), (cg.bool_, "transient")],
            conf,
        )
    cg.add_define("USE_ROOM_COMPANION")
