"""Check Atlas's network prerequisites against the installed ESPHome scheduler.

The engine-only tests cannot reproduce ESP-IDF's uninitialized lwIP semaphore.
Compare the real component priorities: ESPHome calls higher priorities first.
No Wi-Fi connection is required, but its interface and network stack must exist
before Atlas setup starts the ESP-MQTT task.
"""
from pathlib import Path
import re

import esphome

ROOT = Path(__file__).resolve().parents[1]
ESPHOME = Path(esphome.__file__).resolve().parent
constants = dict(re.findall(
    r"inline constexpr float (\w+) = ([\d.]+)f;",
    (ESPHOME / "core/component.h").read_text(),
))


def priority(path, function):
    body = re.search(
        re.escape(function) + r"\(\) const(?: override)?\s*\{([^}]+)\}",
        path.read_text(),
    )
    assert body, f"Cannot resolve setup priority in {path}"
    name = re.search(r"return setup_priority::(\w+);", body.group(1))
    assert name, f"Review changed setup priority expression in {path}"
    return float(constants[name.group(1)])


atlas = priority(ROOT / "components/atlas_pool/atlas_pool.h", "get_setup_priority")
network = priority(ESPHOME / "components/network/network_component.h", "get_setup_priority")
wifi = priority(ESPHOME / "components/wifi/wifi_component.h", "get_setup_priority")
assert atlas < network, "Atlas MQTT must start after lwIP/network initialization"
assert atlas < wifi, "Atlas MQTT must start after Wi-Fi interface initialization"
print("Atlas startup order passed: network and Wi-Fi initialize before MQTT.")
