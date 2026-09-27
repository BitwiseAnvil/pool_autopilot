from homeassistant.components.number import NumberEntity, NumberMode
from . import DOMAIN
from .entity import TankEntity


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    display = hass.data[DOMAIN]
    async_add_entities([TankNumber(display, "capacity_oz", "Tank Capacity"),
                        TankNumber(display, "remaining_oz", "Tank Level")])


class TankNumber(TankEntity, NumberEntity):
    _attr_native_unit_of_measurement = "oz"
    _attr_native_min_value = 0
    _attr_native_max_value = 4096
    _attr_native_step = 0.01
    _attr_mode = NumberMode.BOX

    @property
    def native_value(self):
        return None if self.display.load_failed else self.display.tank.data[self.key]

    async def async_set_native_value(self, value):
        await self.display.change(lambda tank: tank.capacity(value) if self.key == "capacity_oz" else tank.baseline(value))
