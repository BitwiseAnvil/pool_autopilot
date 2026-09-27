"""Run the actual master/slave maintenance and OTA lambdas over a simulated bus."""
from pathlib import Path
import subprocess
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[1]


class Loader(yaml.SafeLoader):
    pass


for tag in ("!lambda", "!secret"):
    Loader.add_constructor(tag, lambda loader, node: loader.construct_scalar(node))
master = yaml.load((ROOT / "pool-doser.yaml").read_text(), Loader=Loader)
slave = yaml.load((ROOT / "pool-doser-slave.yaml").read_text(), Loader=Loader)

# A recovery network must never bypass the RS485 authorization gate.
assert slave["wifi"]["enable_on_boot"] is False
assert slave["wifi"]["reboot_timeout"] == "0s"
assert set(slave["wifi"]) == {
    "id", "ssid", "password", "enable_on_boot", "reboot_timeout"
}
assert not any(key in slave for key in ("api", "mqtt", "web_server", "captive_portal"))
assert len(slave["ota"]) == 1
assert slave["ota"][0]["encryption"]["key"] == "slave_ota_key"
lockout = next(item for item in master["switch"] if item["id"] == "maintenance_lockout")
assert lockout["optimistic"] is False


def actions_to_cpp(actions):
    result = ""
    for action in actions:
        assert len(action) == 1
        kind, value = next(iter(action.items()))
        if kind == "lambda":
            result += f"[&]() {{\n{value}\n}}();\n"
        elif kind in ("script.execute", "switch.turn_off", "script.stop"):
            method = {"script.execute": "execute", "switch.turn_off": "turn_off",
                      "script.stop": "stop"}[kind]
            result += f"id({value}).{method}();\n"
        elif kind == "if":
            result += f"if (([&]() {{ {value['condition']['lambda']} }})()) {{\n"
            result += actions_to_cpp(value["then"]) + "} else {\n"
            result += actions_to_cpp(value.get("else", [])) + "}\n"
        else:
            raise AssertionError(f"Unsupported maintenance action: {action}")
    return result


generated = "#define id(name) name\nnamespace master {\n"
for value in master["globals"]:
    generated += f"{value['type']} {value['id']} = {value['initial_value']};\n"
for section, typename in (("switch", "Switch"), ("binary_sensor", "Switch"),
                          ("script", "Script"), ("text_sensor", "Text")):
    for value in master[section]:
        if "id" in value:
            generated += f"{typename} {value['id']};\n"
generated += "Uart uart; Uart *rs485_uart = &uart;\n"
slave_signal = next(item for item in master["sensor"]
                    if item.get("id") == "slave_wifi_signal_dbm")
generated += f"float slave_signal() {{\n{slave_signal['lambda']}\n}}\n"
for name, actions in (("lock", lockout["turn_on_action"]),
                      ("unlock", lockout["turn_off_action"])):
    generated += f"void {name}() {{\n{actions_to_cpp(actions)}\n}}\n"
for name, duration in (("receive", "50ms"), ("heartbeat", "250ms")):
    actions = next(item["then"] for item in master["interval"]
                   if item["interval"] == duration)
    assert len(actions) == 1 and set(actions[0]) == {"lambda"}
    generated += f"void {name}() {{\n{actions[0]['lambda']}\n}}\n"
scripts = {item["id"]: item for item in master["script"]}
for name in ("force_outputs_safe",):
    generated += f"void actual_{name}() {{\n{actions_to_cpp(scripts[name]['then'])}\n}}\n"
relay_b = next(item for item in master["switch"] if item["id"] == "relay_b")
generated += "void relay_b_off() {\n" + actions_to_cpp(relay_b["turn_off_action"]) + "}\n"
generated += "}\nnamespace slave {\nSwitch relay_b, outlet_live; Wifi slave_wifi;\n"
generated += "Uart uart; Uart *rs485_uart = &uart;\n"
assert len(slave["interval"]) == 1
generated += "void tick() {\n" + slave["interval"][0]["then"][0]["lambda"] + "\n}\n"
for name in ("begin", "error"):
    generated += f"void ota_{name}() {{\n"
    generated += actions_to_cpp(slave["ota"][0][f"on_{name}"]) + "}\n"
generated += "}\n#undef id\n"
for key, value in master["substitutions"].items():
    generated = generated.replace("${" + key + "}", str(value))

with tempfile.TemporaryDirectory(prefix="pooldose-maintenance-") as directory:
    work = Path(directory)
    (work / "maintenance_automation.h").write_text(generated)
    executable = work / "maintenance-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        f"-I{ROOT / 'tests/include'}", f"-I{ROOT}", f"-I{work}",
        str(ROOT / "tests/test_maintenance_wifi.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
