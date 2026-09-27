"""Runs only in isolated HA objects with mocked services; no live controls."""
import asyncio
import copy
import logging

from homeassistant import loader
from homeassistant.components import input_boolean, input_number, input_text
from homeassistant.components.automation import config as automation_config
from homeassistant.const import __version__
from homeassistant.core import Context, HomeAssistant, SupportsResponse
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import trigger
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.script import Script, async_validate_actions_config
from homeassistant.util import dt as dt_util


async def main():
    logging.getLogger("homeassistant.helpers.script").setLevel(logging.CRITICAL)
    hass = HomeAssistant("/tmp/doser-acid-schema-check")
    loader.async_setup(hass)
    await trigger.async_setup(hass)
    input_number.CONFIG_SCHEMA({"input_number": copy.deepcopy(PACKAGE["input_number"])})
    input_text.CONFIG_SCHEMA({"input_text": copy.deepcopy(PACKAGE["input_text"])})
    input_boolean.CONFIG_SCHEMA({"input_boolean": copy.deepcopy(PACKAGE["input_boolean"])})
    checked = await automation_config.async_validate_config(
        hass, {"automation": copy.deepcopy(PACKAGE["automation"])})
    assert len(checked["automation"]) == 2
    await async_validate_actions_config(hass, PACKAGE["script"]["doser_dose"]["sequence"])

    calls, samples, deliveries, reports = [], [], [], []
    tank_scope = {}
    exec(TANK_SOURCE, tank_scope)
    tank = tank_scope["Tank"]()
    tank.baseline(100, 200)
    status = dict(response_version=3, ready=False, average_ph=7.81, active_request_id="", result="none",
                  automatic=True, relay_fault=False, request_token="", requested_oz=2)
    fail_status = fail_accept = fail_report = fail_stop = False
    fail_manual = False
    manual_reply = "accepted"

    async def set_session(call):
        hass.states.async_set("input_text.doser_acid_session", call.data["value"])

    async def pause(call):
        calls.append("pause")

    async def stop(call):
        calls.append("stop")
        if fail_stop:
            raise HomeAssistantError("simulated stop connection loss")
        status.update(ready=False, active_request_id="", result="interrupted", request_token="")

    async def auto_off(call):
        calls.append("auto_off")
        hass.states.async_set("input_boolean.doser_auto_enabled", "off")

    async def observe(call):
        samples.append(dict(call.data))

    async def query(call):
        calls.append("status")
        assert call.data["client_session"] == hass.states.get("input_text.doser_acid_session").state
        if fail_status:
            raise HomeAssistantError("simulated status reply loss")
        return dict(status)

    async def dose(call):
        calls.append("dose")
        request_id = call.data["request_id"]
        assert request_id == status["request_token"] and request_id not in deliveries
        deliveries.append(request_id)
        status.update(ready=False, active_request_id=request_id, result="accepted", request_token="")
        if fail_accept:
            raise HomeAssistantError("simulated acceptance reply loss")
        return {"request_reply": "accepted", **status}

    async def record(call):
        if fail_report:
            raise HomeAssistantError("simulated reporting failure")
        reports.append(dict(call.data["response"]))
        tank.observe(reports[-1])

    async def manual_dose(call):
        if fail_manual:
            raise HomeAssistantError("simulated manual acceptance reply loss")
        return dict(response_version=3, boot_id="manual", accepted_sequence=1,
                    result="accepted", request_reply=manual_reply, requested_oz=call.data["ounces"])

    hass.services.async_register("pool_tank", "record_acceptance", record)
    hass.services.async_register("esphome", "pool_doser_dose", manual_dose,
                                 supports_response=SupportsResponse.OPTIONAL)
    hass.services.async_register("input_text", "set_value", set_session)
    hass.services.async_register("input_boolean", "turn_off", auto_off)
    hass.services.async_register("esphome", "pool_doser_stop", stop)
    hass.services.async_register("esphome", "pool_doser_acid_pause", pause)
    hass.services.async_register("esphome", "pool_doser_acid_observe_ph", observe)
    hass.services.async_register("esphome", "pool_doser_acid_status", query,
                                 supports_response=SupportsResponse.ONLY)
    hass.services.async_register("esphome", "pool_doser_automatic_dose", dose,
                                 supports_response=SupportsResponse.ONLY)

    scripts = []
    for config in PACKAGE["automation"]:
        actions = await async_validate_actions_config(hass, cv.SCRIPT_SCHEMA(copy.deepcopy(config["actions"])))
        scripts.append(Script(hass, actions, config["alias"], "test"))

    def monitoring(**updates):
        attrs = dict(boot="atlas-one", configuration_ok=True, diagnostic_version=2, recovery=False,
                     timestamp_ms=int(dt_util.utcnow().timestamp() * 1000),
                     clock_healthy=True, clock_epoch=1, time_source="time.nist.gov")
        attrs.update(updates)
        hass.states.async_set("sensor.atlas_pool_calibration", "Monitoring", attrs)
        hass.states.async_set("binary_sensor.atlas_pool_maintenance", "off")

    async def reconcile(event="periodic", error=False):
        try:
            await scripts[1].async_run({"trigger": {"id": event}}, context=Context())
        except HomeAssistantError:
            assert error
        else:
            assert not error

    # Before the doser connects (HA startup) or during an outage, both
    # automations end quietly without calling it: no errors, no requests.
    monitoring()
    hass.states.async_set("input_boolean.doser_auto_enabled", "on")
    hass.states.async_set("input_text.doser_acid_session", "")
    for offline in ("unavailable", "unknown"):
        hass.states.async_set("sensor.pool_doser_request_readiness", offline)
        for event in ("startup", "periodic", "reconnect", "intent"):
            await reconcile(event)
        await scripts[0].async_run({"trigger": {"payload_json": {"value": 7.9}}}, context=Context())
        assert not calls and not samples and not deliveries
    assert hass.states.get("input_text.doser_acid_session").state == ""
    hass.states.async_set("sensor.pool_doser_request_readiness", "Paused: fresh_ph_window")
    await reconcile("startup")
    session = hass.states.get("input_text.doser_acid_session").state
    assert not reports
    assert session and calls == ["pause", "status"] and not deliveries
    await reconcile()
    assert hass.states.get("input_text.doser_acid_session").state == session
    await reconcile("reconnect")
    assert calls[-2:] == ["pause", "status"]

    status.update(ready=True, request_token="boot-1")
    fail_status = True
    await reconcile(error=True)
    assert not deliveries  # a failed query cannot use a previous response
    fail_status = False
    fail_accept = True
    await reconcile(error=True)
    assert deliveries == ["boot-1"]
    fail_accept = False
    await reconcile("startup")
    await reconcile("reconnect")
    await reconcile()
    assert deliveries == ["boot-1"]  # lost acceptance: query pending, never resend
    assert not reports and tank.data["remaining_oz"] == 100
    # The controller completed the request and both waits expired during an
    # outage. No timer event is supplied to HA: the periodic query recovers.
    status.update(result="completed", active_request_id="", ready=True, request_token="boot-2")
    await reconcile()
    assert deliveries == ["boot-1", "boot-2"]
    assert len(reports) == 1 and tank.data["remaining_oz"] == 98
    status.update(result="interrupted", active_request_id="", ready=True, request_token="boot-3")
    for blocker in ({"relay_fault": True}, {"response_version": 1}, {"response_version": 2},
                    {"average_ph": 7.80}, {"average_ph": 7.79}, {"average_ph": None},
                    {"average_ph": "nan"}, {"average_ph": "inf"}, {"average_ph": 15}):
        previous = dict(status)
        status.update(blocker)
        await reconcile()
        assert len(deliveries) == 2
        status.clear()
        status.update(previous)
    for state in ("off", "unknown", "unavailable"):
        hass.states.async_set("input_boolean.doser_auto_enabled", state)
        await reconcile()
        assert len(deliveries) == 2
    # Restoring Off cannot launch a pulse even if a controller reports ready.
    hass.states.async_set("input_boolean.doser_auto_enabled", "off")
    await reconcile("startup")
    assert len(deliveries) == 2
    # Turning Auto Off lets a running automatic dose finish; it only pauses
    # observation and prevents new requests. Reconnect and periodic checks too.
    status.update(ready=False, active_request_id="auto-pulse", result="accepted", automatic=True)
    for event in ("intent", "reconnect", "periodic"):
        await reconcile(event)
        assert "stop" not in calls[-3:] and status["active_request_id"] == "auto-pulse"
    assert len(deliveries) == 2
    # Auto Off never aborts deliberately requested manual doses either.
    status.update(active_request_id="manual-pulse", result="accepted", automatic=False)
    await reconcile("intent")
    assert "stop" not in calls[-3:] and status["active_request_id"] == "manual-pulse"
    # STOP disables Auto before the network command; Off remains when it fails.
    actions = await async_validate_actions_config(hass, cv.SCRIPT_SCHEMA(copy.deepcopy(PACKAGE["script"]["doser_stop"]["sequence"])))
    stop_script = Script(hass, actions, "STOP", "test")
    hass.states.async_set("input_boolean.doser_auto_enabled", "on")
    fail_stop = True
    await stop_script.async_run({}, context=Context())
    assert calls[-2:] == ["auto_off", "stop"]
    assert hass.states.get("input_boolean.doser_auto_enabled").state == "off"
    fail_stop = False
    await stop_script.async_run({}, context=Context())
    assert status["active_request_id"] == ""
    hass.states.async_set("input_boolean.doser_auto_enabled", "on")
    await reconcile("intent")
    assert calls[-2:] == ["pause", "status"] and len(deliveries) == 2
    hass.states.async_set("binary_sensor.atlas_pool_maintenance", "on")
    await reconcile()
    assert calls[-2:] == ["pause", "status"] and len(deliveries) == 2

    monitoring()
    now = dt_util.utcnow()
    now_ms = int(now.timestamp() * 1000)
    packet = dict(boot="atlas-one", value=7.8, clock_epoch=1, clock_healthy=True,
                  time_source="time.nist.gov", timestamp_ms=now_ms, measured_at_ms=now_ms)

    async def sample(data):
        await scripts[0].async_run({"trigger": {"payload_json": data}}, context=Context())
        return samples[-1]

    observed = await sample(packet)
    assert observed["valid"] is True and observed["ph"] == 7.8
    assert str(observed["sample_id"]) == str(now_ms) and observed["boot"] == "atlas-one:1"
    # The installed Atlas clock may lead HA slightly between synchronizations.
    future = now_ms + 1000
    monitoring(timestamp_ms=future)
    assert (await sample(dict(packet, measured_at_ms=future, timestamp_ms=future)))["valid"] is True
    monitoring()
    invalid = [dict(packet, measured_at_ms=now_ms - 21000),
               dict(packet, timestamp_ms=now_ms - 11000),
               dict(packet, measured_at_ms=now_ms + 10000),
               dict(packet, boot="atlas-old"), dict(packet, value=None), {},
               dict(packet, clock_healthy=False), dict(packet, clock_epoch=2),
               dict(packet, measured_at_ms=now_ms - 30 * 86400000),
               dict(packet, value="nan"), dict(packet, value="inf"), 42, []]
    for data in invalid:
        assert (await sample(data))["valid"] is False
    for updates in ({"configuration_ok": False}, {"recovery": True},
                    {"timestamp_ms": now_ms - 11000}):
        monitoring(**updates)
        assert (await sample(packet))["valid"] is False
    monitoring()
    hass.states.async_set("binary_sensor.atlas_pool_maintenance", "on")
    assert (await sample(packet))["valid"] is False
    monitoring()
    hass.states.async_set("number.pool_doser_tank_level", "0")
    fail_report = True
    status.update(ready=True, request_token="boot-reporting-failed")
    await reconcile()
    assert deliveries[-1] == "boot-reporting-failed"
    assert tank.data["remaining_oz"] == 98
    fail_report = False
    hass.states.async_set("input_number.doser_dose_oz", "10")
    actions = await async_validate_actions_config(hass, cv.SCRIPT_SCHEMA(copy.deepcopy(PACKAGE["script"]["doser_dose"]["sequence"])))
    manual = Script(hass, actions, "Manual dose", "test")
    await manual.async_run({}, context=Context())
    assert reports[-1]["result"] == "accepted" and reports[-1]["requested_oz"] == 10
    assert tank.data["remaining_oz"] == 88
    manual_reply = "busy"
    await manual.async_run({}, context=Context())
    assert reports[-1]["result"] == "accepted" and reports[-1]["request_reply"] == "busy"
    assert tank.data["remaining_oz"] == 88
    fail_manual = True
    report_count = len(reports)
    try:
        await manual.async_run({}, context=Context())
    except HomeAssistantError:
        pass
    else:
        raise AssertionError("Missing manual response should stop the script")
    # Real status retains the last reply, even across later completion/interruption.
    # None of these status polls may reach tank reporting, including after a refill.
    tank.baseline(200)
    for result in ("accepted", "completed", "interrupted", "none"):
        status.update(result=result, request_reply="accepted", requested_oz=10,
                      ready=False, active_request_id="previous-dose")
        for event in ("periodic", "startup", "reconnect"):
            await reconcile(event)
            assert len(reports) == report_count and tank.data["remaining_oz"] == 200
    print(f"Home Assistant {__version__}: acid package schemas, fresh sample gates, "
          "HA Auto/threshold, STOP/offline intent, immediate tank deductions, rejected/missing replies, "
          "no tank updates from polling/restarts and expired offline waits passed (mock services).")
    await hass.async_stop(force=True)


asyncio.run(main())
