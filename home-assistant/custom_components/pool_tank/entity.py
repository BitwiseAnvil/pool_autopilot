from homeassistant.helpers.entity import Entity


class TankEntity(Entity):
    _attr_should_poll = False
    _attr_has_entity_name = False

    def __init__(self, display, key, name):
        self.display = display
        self._attr_unique_id = "pool_tank_" + key
        self._attr_name = "Pool Doser " + name
        self.key = key

    async def async_added_to_hass(self):
        await super().async_added_to_hass()
        self.display.listeners.add(self.async_write_ha_state)
        self.async_on_remove(lambda: self.display.listeners.discard(self.async_write_ha_state))

    @property
    def extra_state_attributes(self):
        return dict(estimate_basis="Full requested ounces when accepted",
                    storage_error=self.display.error)
