import esphome.codegen as cg
from esphome.components import audio, microphone
import esphome.config_validation as cv
from esphome.const import (
    CONF_BITS_PER_SAMPLE,
    CONF_ID,
    CONF_MICROPHONE,
    CONF_NUM_CHANNELS,
    CONF_SAMPLE_RATE,
)

DEPENDENCIES = ["microphone"]

ns = cg.esphome_ns.namespace("onx_mic_select")
MicSelect = ns.class_("MicSelect", microphone.Microphone, cg.Component)


def _set_stream_limits(config):
    # Mono 16-bit at the source's 16 kHz, like a one-channel PDM microphone.
    config[CONF_BITS_PER_SAMPLE] = 16
    config[CONF_NUM_CHANNELS] = 1
    config[CONF_SAMPLE_RATE] = 16000
    audio.set_stream_limits(
        min_bits_per_sample=16,
        max_bits_per_sample=16,
        min_channels=1,
        max_channels=1,
        min_sample_rate=16000,
        max_sample_rate=16000,
    )(config)
    return config


CONFIG_SCHEMA = cv.All(
    microphone.MICROPHONE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(MicSelect),
            # A stereo 16-bit, 16 kHz microphone (both PDM slots).
            cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _set_stream_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await microphone.register_microphone(var, config)
    source = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_source(source))
