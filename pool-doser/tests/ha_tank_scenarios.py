"""Installed HA code, isolated config directory, real Store and entity platforms."""
import tempfile
import sys
import asyncio
from pathlib import Path

from homeassistant import bootstrap, loader
from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store


async def test_tank_component():
    with tempfile.TemporaryDirectory(prefix="pool-tank-ha-") as directory:
        root = Path(directory)
        component = root / "custom_components/pool_tank"
        component.mkdir(parents=True)
        (component.parent / "__init__.py").write_text("")
        for name, source in TANK_SOURCES.items():
            (component / name).write_text(source)
        sys.path.insert(0, directory)
        hass = HomeAssistant(directory)
        loader.async_setup(hass)
        assert await bootstrap.async_from_config_dict({"pool_tank": {}}, hass)
        await hass.async_block_till_done()
        display = hass.data["pool_tank"]
        assert not hass.services.has_service("pool_tank", "record_status")
        assert hass.states.get("sensor.pool_doser_accounting_status") is None
        assert hass.states.get("sensor.pool_doser_estimated_total_delivered") is None
        assert hass.states.get("number.pool_doser_tank_level")
        await hass.services.async_call("pool_tank", "import_baseline",
                                      dict(remaining_oz=100, capacity_oz=200), blocking=True)
        packet = dict(response_version=3, boot_id="boot", accepted_sequence=1,
                      result="accepted", request_reply="accepted", requested_oz=2.0)
        original_save = display.store.async_save
        saves = []

        async def track_save(data):
            assert set(data) == {"capacity_oz", "remaining_oz"}
            saves.append(dict(data))
            await original_save(data)

        display.store.async_save = track_save

        async def record(value):
            await hass.services.async_call("pool_tank", "record_acceptance", {"response": value}, blocking=True)
            await hass.async_block_till_done()

        await record(packet)
        saved = await Store(hass, 1, "pool_tank").async_load()
        assert saved == {"capacity_oz": 200, "remaining_oz": 98}
        assert hass.states.get("number.pool_doser_tank_level").state == "98.0"
        await record(dict(packet, request_reply="busy", requested_oz=10))
        missing = dict(packet)
        del missing["request_reply"]
        await record(missing)
        assert len(saves) == 1 and display.tank.data == saved
        await record(dict(packet, requested_oz=10))
        for result in ("completed", "interrupted", "none"):
            await record(dict(missing, result=result, boot_id="new-boot", accepted_sequence=0))
        assert len(saves) == 2 and display.tank.data["remaining_oz"] == 88
        await hass.services.async_call("number", "set_value",
                                      dict(entity_id="number.pool_doser_tank_level", value=0), blocking=True)
        await record(packet)
        assert len(saves) == 3 and display.tank.data["remaining_oz"] == 0
        await hass.services.async_call("button", "press",
                                      dict(entity_id="button.pool_doser_tank_refilled"), blocking=True)
        await record(dict(missing, result="completed"))
        assert len(saves) == 4 and display.tank.data["remaining_oz"] == 200
        # Host storage never delays a dosing action; failed reporting stays missed.
        release, entered = asyncio.Event(), asyncio.Event()

        async def fail_save(data):
            entered.set()
            await release.wait()
            raise OSError("simulated host storage failure")

        display.store.async_save = fail_save
        await asyncio.wait_for(hass.services.async_call("pool_tank", "record_acceptance",
                               {"response": packet}, blocking=True), 1)
        await asyncio.wait_for(entered.wait(), 1)
        assert display.tank.data["remaining_oz"] == 200
        release.set()
        await hass.async_block_till_done()
        assert display.error and display.tank.data["remaining_oz"] == 200
        display.store.async_save = track_save
        await record(dict(packet, request_reply="busy"))
        assert len(saves) == 4 and display.tank.data["remaining_oz"] == 200
        # A later accepted call counts only its own requested amount.
        await record(packet)
        assert display.error is None and display.tank.data["remaining_oz"] == 198
        # Responses already received are processed before an operator refill.
        release, entered = asyncio.Event(), asyncio.Event()

        async def slow_save(data):
            entered.set()
            await release.wait()
            await track_save(data)

        display.store.async_save = slow_save
        for _ in range(2):
            await hass.services.async_call("pool_tank", "record_acceptance",
                                           {"response": packet}, blocking=True)
        await asyncio.wait_for(entered.wait(), 1)
        refill = asyncio.create_task(hass.services.async_call("button", "press",
                                     dict(entity_id="button.pool_doser_tank_refilled"), blocking=True))
        while len(display.pending) < 2:
            await asyncio.sleep(0)
        release.set()
        await refill
        await hass.async_block_till_done()
        assert display.tank.data["remaining_oz"] == 200
        assert [data["remaining_oz"] for data in saves[-3:]] == [196, 194, 200]
        display.store.async_save = track_save
        await record(dict(missing, result="interrupted"))
        assert display.tank.data["remaining_oz"] == 200
        await record(packet)
        await record(dict(packet, response_version=99))
        assert display.error and display.tank.data["remaining_oz"] == 198
        storage_path = root / ".storage/pool_tank"
        stored_bytes, stored_mtime = storage_path.read_bytes(), storage_path.stat().st_mtime_ns
        await hass.async_stop(force=True)

        hass = HomeAssistant(directory)
        loader.async_setup(hass)
        assert await bootstrap.async_from_config_dict({"pool_tank": {}}, hass)
        await hass.async_block_till_done()
        display = hass.data["pool_tank"]
        assert display.tank.data == {"capacity_oz": 200, "remaining_oz": 198}
        await record(dict(packet, request_reply="busy"))
        assert storage_path.read_bytes() == stored_bytes
        assert storage_path.stat().st_mtime_ns == stored_mtime
        await display.store.async_save({**display.tank.data, "remaining_oz": -1})
        await hass.async_stop(force=True)

        hass = HomeAssistant(directory)
        loader.async_setup(hass)
        assert await bootstrap.async_from_config_dict({"pool_tank": {}}, hass)
        await hass.async_block_till_done()
        display = hass.data["pool_tank"]
        assert display.load_failed and display.error
        await record(packet)
        saved = await display.store.async_load()
        assert saved == {"capacity_oz": 200, "remaining_oz": -1}
        assert hass.states.get("number.pool_doser_tank_level").state == "unknown"
        await hass.async_stop(force=True)
        sys.path.remove(directory)
    print("HA tank: actual Store/entities, only two stored values, acceptance-only deductions, "
          "no restart writes, refill ordering and nonblocking storage failure passed")


asyncio.run(test_tank_component())
