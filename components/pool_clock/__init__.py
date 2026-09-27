import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components.sntp.time import SNTPComponent
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "time"]
clock_ns = cg.esphome_ns.namespace("pool_clock")
PoolClock = clock_ns.class_("PoolClock", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PoolClock),
    cv.Required("time_id"): cv.use_id(SNTPComponent),
}).extend(cv.COMPONENT_SCHEMA)


def validate_time(config):
    sources = fv.full_config.get().get("time", [])
    if len(sources) != 1 or sources[0].get("platform") != "sntp":
        raise cv.Invalid("Pool calendar clocks require a single SNTP source")
    source = sources[0]
    if source["servers"] != ["time.nist.gov"]:
        raise cv.Invalid("All pool calendar clocks must use only time.nist.gov")
    if source["update_interval"].total_milliseconds != 300000:
        raise cv.Invalid("Pool SNTP synchronization interval must be 5min")
    return config


FINAL_VALIDATE_SCHEMA = validate_time


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_time(await cg.get_variable(config["time_id"])))
