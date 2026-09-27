"""Runs inside the isolated Home Assistant created by test_home_assistant.py."""
import copy

from homeassistant.components.script import config as script_config
from homeassistant.components.template import config as template_config
from homeassistant.core import Context
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.script import Script
from homeassistant.helpers.template import Template


async def test_guided_calibration(hass, package, dashboard):
    validated = await template_config.async_validate_config(hass, {'template': copy.deepcopy(package['template'])})
    assert len(validated['template']) == 1
    validated = await script_config.async_validate_config(hass, {'script': copy.deepcopy(package['script'])})
    assert len(validated['script']) == 2
    screen = package['template'][0]['sensor'][0]['state']
    cards = next(v for v in dashboard['views'] if v['path'] == 'calibration')['cards']
    diagnostic = 'sensor.atlas_pool_calibration'

    def fixture(state='Calibration preview', maintenance='on', **changes):
        attrs = dict(boot='test-boot', token=10, session=4, sensor='RTD', method='rtd',
                     step='single', completed=0, preview=0, preview_unit='C',
                     preview_age_s=1, armed=False, busy=False, recovery=False,
                     configuration_ok=True, diagnostic_version=2, result='', reference=0,
                     controls=dict(reference_ready=False, probes_returned=False,
                                   rtd_reference=0, calibration_method='RTD - optional temperature calibration'))
        attrs.update(changes)
        hass.states.async_set(diagnostic, state, attrs)
        hass.states.async_set('binary_sensor.atlas_pool_maintenance', maintenance)
        hass.states.async_set('sensor.atlas_pool_preview', 'unavailable' if attrs['preview'] is None else str(attrs['preview']))
        hass.states.async_set('switch.atlas_pool_recovery_acknowledged', 'off')
        hass.states.async_set('select.atlas_pool_calibration_method', 'RTD - optional temperature calibration')
        page = Template(screen, hass).async_render(parse_result=False).strip()
        hass.states.async_set('sensor.atlas_pool_calibration_step', page)
        return page

    def visible(card):
        if card['type'] == 'conditional':
            for condition in card['conditions']:
                actual = hass.states.get(condition['entity'])
                actual = actual.state if actual else 'unknown'
                expected = condition.get('state', condition.get('state_not'))
                expected = expected if isinstance(expected, list) else [expected]
                if (actual in expected) != ('state' in condition):
                    return []
            return visible(card['card'])
        if 'cards' in card:
            return [item for child in card['cards'] for item in visible(child)]
        return [card]

    cases = [
        ('Monitoring', 'off', dict(sensor='None', step=''), 'idle', 'Pool monitoring is on', ['Start calibration']),
        ('Calibration preview', 'on', {}, 'rtd', 'Temperature (RTD)', ['Reading is steady', 'Exit calibration']),
        ('Calibration preview', 'on', dict(preview=None), 'rtd', 'Waiting for a fresh reading', ['Exit calibration']),
        ('Calibration preview', 'on', dict(sensor='ORP', preview_unit='mV'), 'orp', '0 mV', ['Reading is steady', 'Exit calibration']),
        ('Calibration preview', 'on', dict(sensor='pH', step='mid'), 'ph_mid', 'midpoint', ['Reading is steady', 'Exit calibration']),
        ('Next reference: low', 'on', dict(sensor='pH', step='low'), 'ph_low', 'low', ['Reading is steady', 'Exit calibration']),
        ('Next reference: high', 'on', dict(sensor='pH', step='high'), 'ph_high', 'high', ['Reading is steady', 'Exit calibration']),
        ('Calibration preview', 'on', dict(sensor='EC', step='dry'), 'ec_dry', 'dry the EC probe', ['Reading is steady', 'Exit calibration']),
        ('Next reference: low', 'on', dict(sensor='EC', step='low'), 'ec_low', 'low', ['Reading is steady', 'Exit calibration']),
        ('Next reference: high', 'on', dict(sensor='EC', step='high'), 'ec_high', 'high point is still required', ['Reading is steady', 'Exit calibration']),
        ('Next reference: single', 'on', dict(sensor='EC'), 'ec_single', 'reference', ['Reading is steady', 'Exit calibration']),
        ('Calibration preview', 'on', dict(armed=True), 'confirm', 'Save this reference', ['Save calibration', 'Exit calibration']),
        ('Verifying reference reading', 'on', dict(busy=True), 'saving', 'Saving and checking', []),
        ('Procedure complete; return required', 'on', dict(step='done', completed=1), 'done', 'Calibration saved', ['Exit calibration']),
        ('Recovery required', 'on', dict(recovery=True, result='Inspect interrupted write'), 'recovery', 'interrupted', ['Exit calibration']),
        ('Calibration cleared', 'on', dict(step='done', recovery=True), 'cleared', 'has been erased', ['Exit calibration']),
        ('Maintenance', 'on', dict(sensor='None', step='', recovery=True), 'maintenance', 'readings are paused', ['Exit calibration']),
        ('Restoring pool readings', 'on', dict(sensor='None', step=''), 'returning', 'Returning to pool readings', []),
        ('Calibration preview', 'on', dict(configuration_ok=False), 'fault', 'needs attention', []),
        ('unavailable', 'unavailable', {}, 'offline', 'Waiting for Atlas', []),
    ]
    for state, maintenance, changes, page, expected, buttons in cases:
        assert fixture(state, maintenance, **changes) == page
        shown = [item for card in cards for item in visible(card)]
        text = '\n'.join(Template(c['content'], hass).async_render(parse_result=False)
                         for c in shown if c['type'] == 'markdown')
        assert expected in text and '****' not in text and 'None' not in text, text
        assert [c['name'] for c in shown if c['type'] == 'button'] == buttons, (page, shown)
        rows = [r['entity'] for c in shown if c['type'] == 'entities' for r in c['entities']]
        if page == 'rtd':
            assert rows == ['number.atlas_pool_rtd_reference'], rows
        if page == 'ph_low':
            assert rows == ['number.atlas_pool_ph_low'], rows
        if page == 'ec_high':
            assert rows == ['number.atlas_pool_ec_high'], rows
        assert len(text.split()) < 110, (page, text)
    fixture(recovery=True)
    hass.states.async_set('switch.atlas_pool_recovery_acknowledged', 'on')
    assert any(c.get('name') == 'Continue calibration' for card in cards for c in visible(card))
    fixture()
    exit_card = next(c for card in cards for c in visible(card) if c.get('name') == 'Exit calibration')
    assert 'ALL probes back in pool water' in exit_card['tap_action']['confirmation']['text']
    assert exit_card['tap_action']['confirmation']['confirm_text'] == 'Yes, exit'
    assert exit_card['tap_action']['confirmation']['dismiss_text'] == 'Stay here'

    # Clear keeps done/zero in RAM. Disconnect replaces the status text;
    # acknowledging recovery then changes recovery/result, but not completion.
    # None of those transitions may turn erased calibration into a saved result.
    for probe, method, completed in [('pH', 'ph_3', 3), ('ORP', 'orp', 1),
                                     ('RTD', 'rtd', 1), ('EC', 'ec_2', 3)]:
        for interrupted_status in ['Recovery required']:
            for status, recovery, result in [
                ('Calibration cleared', True, 'Calibration cleared and read back'),
                (interrupted_status, True, 'Maintenance interrupted'),
                (interrupted_status, False, 'Inspect circuit status and fresh reference readings'),
            ]:
                page = fixture(status, sensor=probe, method=method, step='done',
                               completed=0, recovery=recovery, result=result)
                assert page == 'cleared', (probe, status, recovery, page)
                hass.states.async_set('switch.atlas_pool_recovery_acknowledged', 'on')
                shown = [item for card in cards for item in visible(card)]
                text = '\n'.join(Template(c['content'], hass).async_render(parse_result=False)
                                 for c in shown if c['type'] == 'markdown')
                assert 'Calibration cleared' in text and 'Calibration saved' not in text, text
                assert [c['name'] for c in shown if c['type'] == 'button'] == ['Exit calibration']

            # Successful completion remains distinct, including after recovery.
            assert fixture(interrupted_status, sensor=probe, method=method, step='done',
                           completed=completed, recovery=True) == 'recovery'
            assert fixture(interrupted_status, sensor=probe, method=method, step='done',
                           completed=completed, recovery=False) == 'done'
        # A reboot restores only maintenance intent, never old steps or claims.
        assert fixture('Maintenance: confirmation required', sensor='None', method='',
                       step='', completed=0, recovery=True) == 'maintenance'
        first_step = 'mid' if probe == 'pH' else 'dry' if probe == 'EC' else 'single'
        assert fixture(sensor=probe, method=method, step=first_step, completed=0) not in ['cleared', 'done']

    # Real HA Script runner, mock services and acknowledgements; never live MQTT.
    calls = []
    response = 'accept'

    async def turn_on(call):
        target = call.data['entity_id']
        target = target[0] if isinstance(target, list) else target
        calls.append(target)
        if response == 'timeout':
            return
        current = hass.states.get(diagnostic)
        attrs = copy.deepcopy(dict(current.attributes))
        attrs['token'] += 1
        attrs['result'] = 'Confirmation updated'
        key = target.split('atlas_pool_')[1]
        attrs['controls'][key] = True
        if response == 'refused':
            attrs['result'] = 'Refused: stale preview'
        elif response == 'boot':
            attrs['boot'] = 'different-boot'
        elif response == 'session':
            attrs['session'] += 1
        elif response == 'step':
            attrs['step'] = 'done'
        elif response == 'reference':
            attrs['controls']['rtd_reference'] = 25
        elif response == 'competing_command':
            attrs['token'] += 1
        elif response == 'busy':
            attrs['busy'] = True
        hass.states.async_set(diagnostic, 'unavailable' if response == 'offline' else current.state, attrs)

    async def press(call):
        target = call.data['entity_id']
        calls.append(target[0] if isinstance(target, list) else target)

    hass.services.async_register('switch', 'turn_on', turn_on)
    hass.services.async_register('button', 'press', press)
    for key, switch_key, button_key in [('prepare', 'reference_ready', 'arm'), ('exit', 'probes_returned', 'return')]:
        raw = package['script']['atlas_pool_' + key + '_calibration']
        for response in ['accept', 'refused', 'boot', 'session', 'step', 'reference', 'competing_command', 'busy', 'offline', 'timeout']:
            fixture()
            calls.clear()
            sequence = copy.deepcopy(raw['sequence'])
            # Exercise timeout handling without adding eight seconds to every run.
            for action in sequence:
                if 'wait_template' in action:
                    action['timeout'] = {'milliseconds': 20}
                    assert action['continue_on_timeout'] is False
            runner = Script(hass, cv.SCRIPT_SCHEMA(sequence), raw['alias'], 'script', script_mode=raw['mode'])
            await runner.async_run(context=Context())
            expected_calls = ['switch.atlas_pool_' + switch_key]
            if response == 'accept':
                expected_calls.append('button.atlas_pool_' + button_key)
            assert calls == expected_calls, (key, response, calls)
            assert 'button.atlas_pool_apply' not in calls
        response = 'accept'
        refusals = [dict(busy=True), dict(configuration_ok=False), dict(state='unavailable'),
                    dict(state='Monitoring', maintenance='off', sensor='None', step='')]
        if key == 'prepare':
            refusals += [dict(recovery=True), dict(preview=None), dict(step='done')]
        for changes in refusals:
            fixture(**changes)
            calls.clear()
            runner = Script(hass, cv.SCRIPT_SCHEMA(copy.deepcopy(raw['sequence'])), raw['alias'], 'script')
            await runner.async_run(context=Context())
            assert not calls, (key, changes, calls)
        if key == 'exit':
            for changes in [dict(recovery=True), dict(step='done', recovery=True),
                            dict(sensor='None', step='', recovery=True)]:
                fixture(**changes)
                calls.clear()
                runner = Script(hass, cv.SCRIPT_SCHEMA(copy.deepcopy(raw['sequence'])), raw['alias'], 'script')
                await runner.async_run(context=Context())
                assert calls == ['switch.atlas_pool_probes_returned', 'button.atlas_pool_return'], calls
    print('Guided calibration: 20 screen states, cleared-session recovery transitions, and real HA shortcut tests passed')
