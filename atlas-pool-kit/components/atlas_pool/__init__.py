import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, esp32
from esphome.components.pool_clock import PoolClock
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "i2c", "wifi", "pool_clock"]
AUTO_LOAD = ["json", "network"]
MULTI_CONF = False
atlas_ns = cg.esphome_ns.namespace("atlas_pool_device")
AtlasPool = atlas_ns.class_("AtlasPool", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(AtlasPool),
    cv.Required("i2c_id"): cv.use_id(i2c.I2CBus),
    cv.Required("clock_id"): cv.use_id(PoolClock),
    cv.Required("broker"): cv.string_strict,
    cv.Optional("port", default=1883): cv.port,
    cv.Required("username"): cv.string_strict,
    cv.Required("password"): cv.sensitive(cv.string_strict),
    cv.Optional("device_id", default="atlas_pool"): cv.valid_name,
    cv.Optional("dashboard_path", default=""): cv.string_strict,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_bus(await cg.get_variable(config["i2c_id"])))
    cg.add(var.set_clock(await cg.get_variable(config["clock_id"])))
    cg.add(var.set_mqtt(config["broker"], config["port"], config["username"], config["password"], config["device_id"]))
    cg.add(var.set_dashboard_path(config["dashboard_path"]))
    esp32.include_builtin_idf_component("mqtt")
    esp32.include_builtin_idf_component("esp-tls")
    esp32.add_idf_sdkconfig_option("CONFIG_MQTT_PROTOCOL_5", True)
