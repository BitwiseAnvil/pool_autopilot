"""Check Pool Math extraction, timestamp ages and CSI in isolated HA objects.

Run with Python 3 and Docker. The checks run in a throwaway container from
POOL_HA_IMAGE (default ghcr.io/home-assistant/home-assistant:stable).
No live entities, device controls or configuration are changed.
"""
import os
from pathlib import Path
import subprocess

import yaml

ROOT = Path(__file__).resolve().parents[1]
class Loader(yaml.SafeLoader):
    pass


Loader.add_constructor("!secret", lambda loader, node: "https://example.invalid/" + node.value)
PACKAGE = yaml.load((ROOT / "home-assistant/packages/pool_math.yaml").read_text(), Loader=Loader)

SOURCE = r'''
import asyncio
import copy
from datetime import timedelta
import json

from homeassistant import loader
from homeassistant.core import HomeAssistant, State
from homeassistant.components import recorder, rest
from homeassistant.components.template import config as template_config
from homeassistant.components.rest.util import parse_json_attributes
from homeassistant.helpers import trigger
from homeassistant.helpers.template import Template, TemplateState
from homeassistant.util import dt as dt_util

async def main():
    hass = HomeAssistant('/tmp/pool-math-template-check')
    loader.async_setup(hass)
    await trigger.async_setup(hass)
    rest.CONFIG_SCHEMA({'rest': copy.deepcopy(PACKAGE['rest'])})
    validated = await template_config.async_validate_config(hass, {'template': copy.deepcopy(PACKAGE['template'])})
    assert [len(block['sensor']) for block in validated['template']] == [5, 1]
    recorder.CONFIG_SCHEMA({'recorder': copy.deepcopy(PACKAGE['recorder'])})
    resource = PACKAGE['rest'][0]
    assert resource['resource'] == 'https://example.invalid/pool_math_url'
    assert resource['scan_interval'] == 600
    config = resource['sensor'][0]
    now = dt_util.utcnow()
    timestamp = (now - timedelta(days=9, hours=3)).isoformat()
    values = dict(fc=12.28, ta=90, cya=54, ch=226, bor=43)
    overview = {**values, **{key+'Ts': timestamp for key in values},
                'ph': 1, 'temp': -100, 'salt': 90000, 'csi': 99}
    payload = {'pools': [{'pool': {'overview': overview},
                           'recentLogs': [{'fc': 999, 'ta': 999}]}]}
    attrs = parse_json_attributes(json.dumps(payload), config['json_attributes'],
                                  config['json_attributes_path'])
    assert set(attrs) == set(values) | {key+'Ts' for key in values}
    assert {key: attrs[key] for key in values} == values

    def render(text, variables=None):
        return Template(text, hass).async_render(variables, parse_result=False).strip()

    assert render(config['value_template'], {'value_json':payload}) == 'OK'
    for invalid in [{}, {'pools':[]}, {'pools':[{'pool':{}}]}]:
        assert render(config['value_template'], {'value_json':invalid}) == 'unavailable'
    hass.states.async_set('sensor.pool_math_overview', 'OK', attrs)
    trigger_block, csi_block = PACKAGE['template']
    # Trigger-based template sensors restore state and attributes on restart.
    assert [t['trigger'] for t in trigger_block['triggers']] == ['state', 'time_pattern']
    assert trigger_block['triggers'][0]['entity_id'] == 'sensor.pool_math_overview'
    readings = trigger_block['sensor']
    csi = csi_block['sensor'][0]
    assert len(readings) == 5 and 'triggers' not in csi_block

    def previous(state='unknown', attributes=None):
        return TemplateState(hass, State('sensor.pool_math_free_chlorine', state, attributes or {}))

    def entity(sensor, this):
        return (render(sensor['state'], {'this': this}),
                render(sensor['attributes']['tested_at'], {'this': this}),
                render(sensor['attributes']['reading'], {'this': this}))

    for sensor, key in zip(readings, values):
        state, tested_at, reading = entity(sensor, previous())
        assert float(state) == values[key]
        assert reading == f'{values[key]:g} ppm — 9 days ago'
        assert tested_at == timestamp

    # Keep the last valid reading through failures, rate limits and restarts.
    kept = previous('12.28', {'tested_at': timestamp})
    for overview, bad in [('unavailable', {}), ('OK', dict(fc=None)), ('OK', dict(fcTs=None)),
                          ('OK', dict(fcTs='invalid')), ('OK', dict(fc='text'))]:
        hass.states.async_set('sensor.pool_math_overview', overview, dict(attrs, **bad))
        assert entity(readings[0], kept) == ('12.28', timestamp, '12.28 ppm — 9 days ago'), bad
    hass.states.async_set('sensor.pool_math_overview', 'unavailable', {})
    assert entity(readings[0], previous()) == ('None', 'None', 'None')
    # Ages advance from the kept test time with no new Pool Math data.
    for days in [0, 1, 2, 35]:
        tested = (now - timedelta(days=days, hours=1)).isoformat()
        reading = entity(readings[0], previous('12', {'tested_at': tested}))[2]
        assert reading.endswith(f'{days} {"day" if days == 1 else "days"} ago'), reading
    # A newer valid test replaces the kept value.
    newer = (now - timedelta(hours=2)).isoformat()
    hass.states.async_set('sensor.pool_math_overview', 'OK', dict(attrs, fc=9.5, fcTs=newer))
    assert entity(readings[0], kept) == ('9.5', newer, '9.5 ppm — 0 days ago')

    def inputs(ph=7.8, ta=90, ch=226, cya=54, salt=4000, bor=43, temp=85, unit='°F'):
        hass.states.async_set('sensor.atlas_pool_ph', ph, {'unit_of_measurement':'pH'})
        hass.states.async_set('sensor.atlas_pool_temperature', temp, {'unit_of_measurement':unit})
        hass.states.async_set('sensor.atlas_pool_salinity', salt, {'unit_of_measurement':'ppm'})
        for entity_id, value in [('total_alkalinity', ta), ('calcium_hardness', ch),
                                 ('cya', cya), ('borates', bor)]:
            hass.states.async_set('sensor.pool_math_' + entity_id,
                                  'unknown' if value is None else value)

    # Golden outputs evaluated with TFP's published JavaScript CSI() function.
    for args, expected in [
        ((7.5, 100, 260, 40, 0, 0, 84), 0.02),
        ((7.8, 90, 226, 54, 4000, 43, 85), -0.14),
        ((7.5, 80, 300, 50, 3500, 50, 80), -0.36),
        ((7.2, 60, 150, 70, 5000, 0, 60), -1.35),
        ((8, 120, 500, 30, 0, 0, 95), 0.94),
    ]:
        inputs(*args)
        assert render(csi['availability']) == 'True'
        result = float(render(csi['state']))
        assert result == expected, (args, result, expected)
        inputs(*args[:-1], temp=(args[-1] - 32) * 5 / 9, unit='°C')
        assert float(render(csi['state'])) == expected

    inputs()
    initial = float(render(csi['state']))
    inputs(ph=7.9)
    assert float(render(csi['state'])) != initial
    inputs(temp=85.9)
    assert float(render(csi['state'])) != initial, 'Keep fractional Atlas temperature'
    # CSI follows the kept test readings, so a failed fetch never blanks it.
    hass.states.async_set('sensor.pool_math_overview', 'unavailable', {})
    assert render(csi['availability']) == 'True'
    dependencies = Template(csi['state'], hass).async_render_to_info().entities
    assert dependencies == {'sensor.atlas_pool_ph', 'sensor.atlas_pool_temperature',
                            'sensor.atlas_pool_salinity', 'sensor.pool_math_total_alkalinity',
                            'sensor.pool_math_calcium_hardness', 'sensor.pool_math_cya',
                            'sensor.pool_math_borates'}
    for bad in [dict(ph='unavailable'), dict(temp='unknown'), dict(salt='nan'),
                dict(ta=None), dict(cya=None), dict(ch=0), dict(bor=None),
                dict(ta=1, cya=100), dict(unit='K')]:
        inputs(**bad)
        assert render(csi['availability']).lower() == 'false', bad
    print('Pool Math: HA schemas, overview-only extraction, original timestamps, ages,')
    print('kept last values through failures, TFP CSI vectors, °F/°C and missing inputs passed')
    await hass.async_stop(force=True)

asyncio.run(main())
'''

if __name__ == "__main__":
    # A throwaway container; no running Home Assistant instance is touched.
    image = os.environ.get("POOL_HA_IMAGE", "ghcr.io/home-assistant/home-assistant:stable")
    subprocess.run(["docker", "run", "--rm", "-i", "--entrypoint", "python", image, "-"],
                   input="PACKAGE = " + repr(PACKAGE) + "\n" + SOURCE,
                   text=True, check=True)
