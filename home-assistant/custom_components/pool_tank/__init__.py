"""Tank display only; reporting never participates in dosing admission."""
from collections import deque
from copy import deepcopy
import logging

import voluptuous as vol
from homeassistant.helpers import discovery
from homeassistant.helpers.storage import Store

from .tank import Tank

DOMAIN = "pool_tank"
CONFIG_SCHEMA = vol.Schema({DOMAIN: vol.Schema({})}, extra=vol.ALLOW_EXTRA)
_LOGGER = logging.getLogger(__name__)


class TankDisplay:
    def __init__(self, hass):
        self.hass = hass
        self.store = Store(hass, 1, DOMAIN)
        self.tank = Tank()
        self.listeners = set()
        self.error = None
        self.load_failed = False
        self.pending = deque()
        self.worker = None

    def submit(self, operation):
        done = self.hass.loop.create_future()
        self.pending.append((operation, done))
        if self.worker is None or self.worker.done():
            self.worker = self.hass.async_create_task(self.collect())
        return done

    async def change(self, operation):
        await self.submit(operation)

    async def collect(self):
        try:
            while self.pending:
                operation, done = self.pending.popleft()
                try:
                    if self.load_failed:
                        raise ValueError("Tank storage could not be loaded; preserve it for repair")
                    candidate = Tank(self.tank.data)
                    operation(candidate)
                    if candidate.data != self.tank.data:
                        # Save only capacity and level. Process immediate dose
                        # responses and operator corrections in arrival order.
                        await self.store.async_save(candidate.data)
                        self.tank = candidate
                    self.error = None
                except Exception as err:
                    self.error = str(err)
                    _LOGGER.error("Tank display update failed: %s", err)
                for update in self.listeners:
                    update()
                if not done.done():
                    done.set_result(None)
        finally:
            self.worker = None


async def async_setup(hass, config):
    display = hass.data[DOMAIN] = TankDisplay(hass)
    try:
        display.tank = Tank(await display.store.async_load())
    except Exception as err:
        display.error = str(err)
        display.load_failed = True
        _LOGGER.error("Tank storage unavailable: %s", err)

    async def record(call):
        # Return immediately so host storage never delays a dosing action.
        if len(display.pending) >= 128:
            _LOGGER.warning("Tank display backlog full; skipping acceptance")
            return
        packet = deepcopy(call.data["response"])
        display.submit(lambda tank: tank.observe(packet))

    async def import_baseline(call):
        await display.change(lambda tank: tank.baseline(call.data["remaining_oz"], call.data["capacity_oz"]))

    hass.services.async_register(DOMAIN, "record_acceptance", record,
                                 schema=vol.Schema({vol.Required("response"): dict}))
    hass.services.async_register(DOMAIN, "import_baseline", import_baseline,
                                 schema=vol.Schema({vol.Required("remaining_oz"): vol.Coerce(float),
                                                    vol.Required("capacity_oz"): vol.Coerce(float)}))
    for platform in ("number", "button"):
        await discovery.async_load_platform(hass, platform, DOMAIN, {}, config)
    return True
