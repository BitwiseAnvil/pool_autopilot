"""Source contracts complement execution of the full YAML scheduler."""
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]
class Loader(yaml.SafeLoader):
    pass
for tag in ("!lambda", "!secret"):
    Loader.add_constructor(tag, lambda loader, node: loader.construct_scalar(node))
source = (ROOT / "pool-doser.yaml").read_text()
master = yaml.load(source, Loader=Loader)
slave = yaml.load((ROOT / "pool-doser-slave.yaml").read_text(), Loader=Loader)
scripts = {s["id"]: s for s in master["script"]}
for name in ("operator_stop", "stop_dose", "latch_relay_fault_safely"):
    assert scripts[name]["then"][0] == {"script.execute": "force_outputs_safe"}
assert scripts["force_outputs_safe"]["then"][:2] == [
    {"switch.turn_off": "relay_a"}, {"switch.turn_off": "relay_b"}]
for obsolete in ("rolling_usage", "max_24h", "tank_level", "PersistentSafety", "reserve_delivery",
                 "reset_dose_accounting", "persistent_safety", "reservation_persisted", "auto_enabled"):
    assert obsolete not in source
boot = str(master["esphome"]["on_boot"])
assert "CLEAR_FAULT" not in boot and "save_settings" not in boot
for name in ("refresh_acid_status", "finalize_delivery", "clear_relay_fault", "run_sequence", "operator_stop"):
    assert "save_settings" not in str(scripts[name])
for filename in ("pool_doser_rs485.h", "pool_doser_control.h"):
    header = (ROOT / filename).read_text()
    assert "auto_enabled" not in header and "ACID_TARGET_PH" not in header
assert next(a for a in master["api"]["actions"] if a["action"] == "stop")["then"] == [
    {"script.execute": "operator_stop"}]
assert len(master["number"]) == 5
for number in master["number"]:
    assert number["optimistic"] is False and number["restore_value"] is False
    assert "state_edit_allowed" in str(number["set_action"])
assert source.count("pool_doser::DOSE_B,\n                                          id(rs485_transaction), id(dose_total_ms)") == 1
for config in (master, slave):
    assert config["safe_mode"]["storage"] == "rtc"
    assert "pool_storage" in config
    assert config["esp32"]["toolchain"] == "esp-idf"
    for item in config["switch"]:
        assert item.get("restore_mode") in (None, "ALWAYS_OFF", "DISABLED")
assert slave["wifi"]["enable_on_boot"] is False
assert "time" not in slave
print("Shutdown ordering, sole energizing path, RAM runtime and explicit save contracts passed")
