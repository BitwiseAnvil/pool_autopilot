"""Actual HA helper restoration across isolated starts/stops, without live devices."""
import asyncio
import copy
import tempfile

from homeassistant import bootstrap, loader
from homeassistant.core import HomeAssistant


async def main():
    entity = "input_boolean.doser_auto_enabled"
    with tempfile.TemporaryDirectory(prefix="pool-auto-ha-") as directory:
        # Start without any stored intent, save On, restore On and save Off,
        # then restore Off. Use HA's own input_boolean and restore-state storage.
        for expected, change in (("off", "turn_on"), ("on", "turn_off"), ("off", None)):
            hass = HomeAssistant(directory)
            loader.async_setup(hass)
            assert await bootstrap.async_from_config_dict(
                {"input_boolean": copy.deepcopy(PACKAGE["input_boolean"])}, hass)
            await hass.async_start()
            await hass.async_block_till_done()
            assert hass.states.get(entity).state == expected
            if change:
                await hass.services.async_call("input_boolean", change,
                                              {"entity_id": entity}, blocking=True)
                assert hass.states.get(entity).state != expected
            await hass.async_stop(force=True)
    print("HA Auto helper: default Off, restored On and restored Off across actual HA restarts passed")

    # Last Dose is kept by HA: it follows new doser values, ignores the doser's
    # unknown/unavailable after a doser restart, and survives HA restarts.
    source, kept = "sensor.pool_doser_last_delivered", "sensor.pool_doser_last_dose"
    with tempfile.TemporaryDirectory(prefix="pool-last-dose-ha-") as directory:
        for step in range(3):
            hass = HomeAssistant(directory)
            loader.async_setup(hass)
            assert await bootstrap.async_from_config_dict(
                {"template": copy.deepcopy(PACKAGE["template"])}, hass)
            await hass.async_start()
            await hass.async_block_till_done()

            async def report(value):
                hass.states.async_set(source, value, {"unit_of_measurement": "oz"})
                await hass.async_block_till_done()
                return hass.states.get(kept).state

            if step == 0:
                assert hass.states.get(kept).state == "unknown"
                assert await report("1.9996") == "2.0"
                for lost in ("unknown", "unavailable"):
                    assert await report(lost) == "2.0"
                assert await report("0.73") == "0.73"
            else:
                # Restored after the restart, then kept through doser restarts.
                assert hass.states.get(kept).state == "0.73"
                assert await report("unavailable") == "0.73"
                assert await report("unknown") == "0.73"
            await hass.async_stop(force=True)
    print("HA Last Dose: new doser values, kept through doser restarts and actual HA restarts passed")


asyncio.run(main())
