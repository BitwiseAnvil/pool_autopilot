from homeassistant.components.button import ButtonEntity
from . import DOMAIN
from .entity import TankEntity


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    async_add_entities([Refill(hass.data[DOMAIN], "refill", "Tank Refilled")])


class Refill(TankEntity, ButtonEntity):
    _attr_icon = "mdi:barrel-outline"

    async def async_press(self):
        await self.display.change(lambda tank: tank.baseline(tank.data["capacity_oz"]))
