"""Validate production discovery payloads, the optional dashboard and HA templates.

--container runs isolated Home Assistant objects in a throwaway Docker container
(image from POOL_HA_IMAGE, default ghcr.io/home-assistant/home-assistant:stable).
It mounts nothing, has no network and never touches a live installation.
"""
from pathlib import Path
import argparse
import datetime
import json
import os
import re
import subprocess
import tempfile
from types import SimpleNamespace

import yaml
from jinja2 import StrictUndefined
from jinja2.nativetypes import NativeEnvironment

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = yaml.safe_load((ROOT / "home-assistant-package.yaml").read_text())
DASHBOARD = yaml.safe_load((ROOT / "home-assistant-dashboard.yaml").read_text())


def discovery_payloads(readings=False):
    with tempfile.TemporaryDirectory(prefix="atlas-discovery-test-") as temporary:
        binary = str(Path(temporary) / "export-controls")
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        str(ROOT / "tests/export_controls.cpp"), "-o", binary], check=True)
        return json.loads(subprocess.check_output([binary] + (["readings"] if readings else []), text=True))


def test_bindings(configs):
    component = (ROOT / "components/atlas_pool/atlas_pool.cpp").read_text()
    keys = re.search(r'const char \*keys\[\]=\{([^;]+)\};', component).group(1)
    names = {"ph", "orp", "temperature", "conductivity", "salinity", "tds"}
    names.update(re.findall(r'"([a-z_]+)"', keys))
    known = {"sensor.atlas_pool_" + key for key in names}
    known.add("binary_sensor.atlas_pool_maintenance")
    assert len(configs) == 27
    control_ids = {config["default_entity_id"] for config in configs}
    assert len(control_ids) == len(configs)
    assert len({config["unique_id"] for config in configs}) == len(configs)
    known.update(control_ids)
    known.add("sensor.atlas_pool_calibration_step")
    known.update("script." + key for key in PACKAGE["script"])
    assert set(PACKAGE) == {"recorder", "pool_telemetry", "template", "script"}
    for config in configs:
        assert config["device"]["identifiers"] == ["atlas_pool"]
        assert config["retain"] is False and config["qos"] == 0
        assert config["command_topic"] == "pool/atlas_pool/command"
        assert config["entity_category"] == "config"
        assert 'script.' not in config['command_template'], "Native controls remain independent of the guided UI"
    dashboard = (ROOT / "home-assistant-dashboard.yaml").read_text()
    assert not re.search(r'(?:input_number|input_boolean|input_select)\.atlas_pool_', dashboard)
    for entity in re.findall(r'(?:binary_sensor|sensor|number|switch|button|select|script)\.atlas_pool_[a-z0-9_]+', dashboard):
        assert entity in known, entity
    assert control_ids <= set(re.findall(r'(?:number|switch|button|select)\.atlas_pool_[a-z0-9_]+', dashboard))
    assert "sensor.atlas_pool_preview" in PACKAGE["recorder"]["exclude"]["entities"]
    assert not any("doser" in key for key in known)


def test_templates(configs):
    attrs = dict(atlas_device="atlas_pool", boot="boot-abc", token=123, session=7, uptime_ms=23000, step="mid")
    env = NativeEnvironment(undefined=StrictUndefined)
    env.filters["to_json"] = json.dumps
    env.globals.update(states=SimpleNamespace(sensor=[SimpleNamespace(attributes=attrs)]),
                       now=lambda: datetime.datetime.now(datetime.timezone.utc), range=range)
    env.globals["utcnow"] = env.globals["now"]
    for config in configs:
        domain, key = config["default_entity_id"].split(".atlas_pool_")
        value = config["options"][0] if domain == "select" else "ON" if domain == "switch" else "7.01"
        rendered = env.from_string(config["command_template"]).render(value=value)
        payload = json.loads(rendered) if isinstance(rendered, str) else rendered
        assert payload["control"] is True
        assert datetime.datetime.fromisoformat(payload["timestamp"]).utcoffset() == datetime.timedelta(0)
        assert {name: payload[name] for name in ("boot", "token", "session", "issued", "step")} == {
            "boot": "boot-abc", "token": 123, "session": 7, "issued": 23000, "step": "mid"}
        assert payload["action"] == (key if domain == "button" else "set")
        if domain != "button":
            assert payload["key"] == key and payload["value"] == value


def test_container(configs):
    readings = discovery_payloads(readings=True)
    source = "READINGS = " + repr(readings) + "\nCONFIGS = " + repr(configs) + "\nPACKAGE = " + repr(PACKAGE) + "\nDASHBOARD = " + repr(DASHBOARD) + "\n" + r'''
import asyncio
import copy
import json
from homeassistant.core import HomeAssistant
from homeassistant import loader
from homeassistant.helpers import trigger
from homeassistant.helpers.template import Template
from homeassistant.components.automation import config as automation_config
from homeassistant.components import recorder
from homeassistant.components.mqtt import button, number, select, switch, sensor
from homeassistant.util import dt as dt_util
from homeassistant.const import __version__

async def main():
    hass = HomeAssistant('/tmp/atlas-schema-check')
    loader.async_setup(hass)
    await trigger.async_setup(hass)
    recorder.CONFIG_SCHEMA({'recorder': PACKAGE['recorder']})
    modules = dict(button=button, number=number, select=select, switch=switch)
    attrs = dict(atlas_device='atlas_pool', boot='native-test', token=17, session=4, uptime_ms=23000, step='mid')
    # No package or default entity ID exists. Discovery must work after a rename.
    hass.states.async_set('sensor.renamed_atlas_status','Calibration preview', attrs)
    hass.states.async_set('sensor.another_atlas','Monitoring', {**attrs, 'atlas_device':'another_device', 'token':99})
    for raw in CONFIGS:
        domain, key = raw['default_entity_id'].split('.atlas_pool_')
        modules[domain].DISCOVERY_SCHEMA(copy.deepcopy(raw))
        values = raw['options'] if domain=='select' else ['ON','OFF'] if domain=='switch' else ['0','7.01','-25'] if domain=='number' else ['PRESS']
        for value in values:
            payload = json.loads(Template(raw['command_template'],hass).async_render({'value':value}, parse_result=False))
            assert payload['control'] is True and payload['boot']=='native-test' and payload['token']==17
            assert payload['session']==4 and payload['issued']==23000 and payload['step']=='mid'
            assert payload['action'] == (key if domain=='button' else 'set')
            if domain!='button':
                assert payload['key']==key and payload['value']==value
                state = value=='ON' if domain=='switch' else float(value) if domain=='number' else value
                rendered = Template(raw['value_template'],hass).async_render({'value_json':{'controls':{key:state}}}, parse_result=False)
                if domain=='number':
                    assert float(rendered)==float(value)
                else:
                    assert rendered==value
    hass.states.async_remove('sensor.renamed_atlas_status')
    payload = json.loads(Template(CONFIGS[-1]['command_template'],hass).async_render({'value':'PRESS'}, parse_result=False))
    assert payload['boot']=='' and payload['token']==0
    await test_guided_calibration(hass, PACKAGE, DASHBOARD)
    await test_telemetry_runtime(hass)
    now_ms = int(dt_util.utcnow().timestamp() * 1000)
    for raw in READINGS:
        sensor.DISCOVERY_SCHEMA(copy.deepcopy(raw))
        good = dict(value=7.8, measured_at_ms=now_ms, clock_healthy=True, time_source='time.nist.gov')
        for payload, valid in [(good, True), (dict(good, measured_at_ms=now_ms-20000), False),
                               (dict(good, measured_at_ms=now_ms-30*86400000), False),
                               (dict(good, measured_at_ms=now_ms+10000), False),
                               ({'value':None}, False)]:
            variables = {'value_json':payload}
            rendered = Template(raw['value_template'],hass).async_render(variables, parse_result=False)
            availability = Template(raw['availability'][1]['value_template'],hass).async_render(variables, parse_result=False)
            assert rendered == ('7.8' if valid else 'None')
            assert availability == ('online' if valid else 'offline')
    print(f'Home Assistant {__version__}: all 27 production MQTT discovery schemas and command/state templates passed')
    print('Native controls work independently and with renamed diagnostics; guided dashboard and six measurement freshness templates passed')
    await hass.async_stop(force=True)

asyncio.run(main())
'''
    telemetry = ROOT.parent / "home-assistant/custom_components/pool_telemetry"
    source = ("TELEMETRY_VALIDATION = " + repr((telemetry / "validation.py").read_text()) + "\n" +
              "TELEMETRY_COMPONENT = " + repr((telemetry / "__init__.py").read_text()) + "\n" + source)
    guided_tests = (ROOT / "tests/test_guided_calibration.py").read_text()
    guided_tests += "\n" + (ROOT / "tests/test_telemetry_runtime.py").read_text()
    source = source.replace("asyncio.run(main())", guided_tests + "\nasyncio.run(main())")
    # Throwaway, offline container: all sources arrive on stdin and nothing is mounted.
    image = os.environ.get("POOL_HA_IMAGE", "ghcr.io/home-assistant/home-assistant:stable")
    subprocess.run(["docker", "run", "--rm", "-i", "--network", "none", "--entrypoint", "python",
                    image, "-"], input=source, text=True, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--container", action="store_true")
    args = parser.parse_args()
    configs = discovery_payloads()
    test_bindings(configs)
    test_templates(configs)
    print("Home Assistant discovery, dashboard bindings and all 27 control envelopes passed")
    if args.container:
        test_container(configs)
