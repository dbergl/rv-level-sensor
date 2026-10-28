from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity import Entity

from .const import CONF_DEVICE_ID, DOMAIN
from .runtime import RvLevelConfigEntry


class RvLevelEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, entry: RvLevelConfigEntry, key: str) -> None:
        self.entry = entry
        self.runtime = entry.runtime_data
        self._attr_translation_key = key
        self._attr_unique_id = f"{entry.entry_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name="RV Level",
            manufacturer="DIY",
            model="RV Level",
            via_device_id=entry.data[CONF_DEVICE_ID],
        )

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(self.runtime.async_add_listener(self.async_write_ha_state))
