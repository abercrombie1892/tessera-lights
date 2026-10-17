"""Lights as tiles that are their own slider, on a ground of the light's own colour: the ESPHome side. register_plugin()
reads the id, version, tile memory and the screen's texts from the manifest and translations/ beside this folder."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

tessera_lights_ns = cg.esphome_ns.namespace("tessera_lights")
TesseraLights = tessera_lights_ns.class_("TesseraLights", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(TesseraLights)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
