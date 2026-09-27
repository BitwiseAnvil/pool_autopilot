"""Compile and execute every production Master script and the Slave loop.

The tiny cooperative scheduler interprets ESPHome delay/wait/if primitives;
all decisions, relay commands, and state changes come from the actual YAML.
"""
from pathlib import Path
import subprocess
import tempfile
import yaml

ROOT = Path(__file__).resolve().parents[1]


class Lambda(str):
    pass


class Loader(yaml.SafeLoader):
    pass


Loader.add_constructor("!lambda", lambda loader, node: Lambda(loader.construct_scalar(node)))
Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
config = yaml.load((ROOT / "pool-doser.yaml").read_text(), Loader=Loader)
slave = yaml.load((ROOT / "pool-doser-slave.yaml").read_text(), Loader=Loader)


def expression(value):
    if isinstance(value, Lambda):
        return f"([&]() {{ {value} }})()"
    if isinstance(value, str):
        return '"' + value + '"'
    return str(value).lower()


def duration(value):
    if isinstance(value, Lambda):
        return expression(value)
    return str(int(float(value[:-2]))) if value.endswith("ms") else str(int(float(value[:-1]) * 1000))


def program(actions):
    cases = []

    def walk(actions):
        for action in actions:
            kind, value = next(iter(action.items()))
            index = len(cases)
            cases.append("")
            if kind == "if":
                condition = value["condition"]["lambda"]
                then_start = len(cases)
                walk(value["then"])
                jump = len(cases)
                cases.append("")
                otherwise = len(cases)
                walk(value.get("else", []))
                end = len(cases)
                cases[index] = f"jump_to(([&]() {{ {condition} }})() ? {then_start} : {otherwise}); break;"
                cases[jump] = f"jump_to({end}); break;"
                continue
            if kind == "lambda":
                body = f"[&]() {{ {value} }}();"
            elif kind == "delay":
                body = f"if (!delay_ready(uint32_t({duration(value)}))) return;"
            elif kind == "wait_until":
                body = f"if (!([&]() {{ {value['condition']['lambda']} }})() && uint32_t(millis() - entered) < {duration(value['timeout'])}U) return;"
            elif kind == "script.wait":
                body = f"if ({value}.is_running()) return;"
            elif kind in ("script.execute", "script.stop", "switch.turn_off", "switch.turn_on"):
                method = {"script.execute": "execute", "script.stop": "stop", "switch.turn_off": "turn_off", "switch.turn_on": "turn_on"}[kind]
                if isinstance(value, dict):
                    name = value["id"]
                    target = next(s for s in config["script"] if s["id"] == name)
                    params = ", ".join(expression(value[p]) for p in target.get("parameters", {}))
                else:
                    name, params = value, ""
                body = f"{name}.{method}({params});"
            elif kind in ("text_sensor.template.publish", "sensor.template.publish"):
                body = f"{value['id']}.publish_state({expression(value['state'])});"
            elif kind == "logger.log":
                body = ""  # logs have no control side effects
            else:
                raise AssertionError(f"Unimplemented automation primitive: {kind}")
            cases[index] = body + f"\nif (!running || stage != {index}) return; jump_to({index + 1}); break;"
    walk(actions)
    return "\n".join(f"case {i}: {{\n{case}\n}}" for i, case in enumerate(cases))


generated = "#define id(name) name\nnamespace master {\nClock nist_clock; Uart uart; Uart *rs485_uart = &uart;\n"
for value in config["globals"]:
    generated += f"{value['type']} {value['id']} = {value['initial_value']};\n"
for section, typename in (("number", "Number"), ("sensor", "Number"), ("text_sensor", "Text"), ("switch", "Switch"), ("binary_sensor", "Switch")):
    for value in config[section]:
        if "id" in value:
            generated += f"{typename} {value['id']};\n"
for script in config["script"]:
    name = script["id"]
    parameters = script.get("parameters", {})
    fields = " ".join(f"{'std::string' if t == 'string' else t} {p}{{}};" for p, t in parameters.items())
    args = ", ".join(f"{'std::string' if t == 'string' else t} arg_{p}" for p, t in parameters.items())
    assign = " ".join(f"{p} = arg_{p};" for p in parameters)
    guard = "if (running) return;" if script["mode"] == "single" else ""
    generated += f"struct S_{name} : Script {{ {fields} void execute({args}) {{ {guard} {assign} running = true; jump_to(0); tick(); }} void tick() override; }} {name};\n"
for script in config["script"]:
    generated += f"void S_{script['id']}::tick() {{ while (running) {{ switch (stage) {{\n{program(script['then'])}\ndefault: stop(); return;\n}} }} }}\n"
generated += "void scripts_tick() {\n" + "\n".join(s["id"] + ".tick();" for s in config["script"]) + "\n}\n"
for name, interval in (("receive", "50ms"), ("heartbeat", "250ms")):
    body = next(s["then"][0]["lambda"] for s in config["interval"] if s["interval"] == interval)
    generated += f"void {name}() {{\n{body}\n}}\n"
for value in config["number"]:
    generated += f"void set_{value['id']}(float x) {{\n{value['set_action'][0]['lambda']}\n}}\n"
for name in ("maintenance_lockout",):
    value = next(s for s in config["switch"] if s["id"] == name)
    for key in ("turn_on_action", "turn_off_action"):
        # Reuse the full scheduler for switch actions with embedded branches.
        func = name + ("_on" if key == "turn_on_action" else "_off")
        generated += f"void {func}() {{ struct Action : Script {{ void tick() override {{ while(running) {{ switch(stage) {{ {program(value[key])} default: stop(); return; }} }} }} }} a; a.running=true; a.tick(); }}\n"
restore = next(b for b in config["esphome"]["on_boot"] if b["priority"] == -100)["then"][0]["lambda"]
generated += f"void boot() {{\n{restore}\n}}\n"
installation = next(a for a in config["api"]["actions"] if a["action"] == "import_operating_settings")
arguments = ", ".join(f"float {key}" for key in installation["variables"])
generated += f"void import_settings({arguments}) {{\n{installation['then'][0]['lambda']}\n}}\n"
relay_b = next(s for s in config["switch"] if s["id"] == "relay_b")
generated += "void relay_b_off() {\n" + relay_b["turn_off_action"][0]["lambda"] + "\n}\n"
generated += "void reset_ram() {\n"
for value in config["globals"]:
    generated += f"{value['id']} = {value['initial_value']};\n"
for value in config["script"]:
    generated += f"{value['id']}.stop();\n"
generated += "}\n}\nnamespace slave {\nSwitch relay_b, outlet_live; Wifi slave_wifi; Uart uart; Uart *rs485_uart = &uart;\n"
generated += "void tick() {\n" + slave["interval"][0]["then"][0]["lambda"] + "\n}\n}\n#undef id\n"
for key, value in config["substitutions"].items():
    generated = generated.replace("${" + key + "}", str(value))
generated = generated.replace("pool_doser::delivery_start_interlocks_passed(", "trace_interlocks(")
with tempfile.TemporaryDirectory(prefix="pooldose-runtime-") as directory:
    work = Path(directory)
    (work / "runtime_automation.h").write_text(generated)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    f"-I{ROOT / 'tests/include'}", f"-I{ROOT}", f"-I{work}",
                    str(ROOT / "tests/test_runtime_state.cpp"), "-o", str(work / "runtime-tests")], check=True)
    subprocess.run([str(work / "runtime-tests")], check=True, timeout=90)
