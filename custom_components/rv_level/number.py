from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import (
    CONF_BLOCK_HEIGHT,
    CONF_BLOCKS_CARRIED,
    CONF_TOLERANCE,
    CONF_TRACK,
    CONF_UNIT,
    CONF_WHEELBASE,
    SETTINGS,
)
from .entity import RvLevelEntity
from .runtime import RvLevelConfigEntry

KEYS = (
    CONF_WHEELBASE,
    CONF_TRACK,
    CONF_BLOCK_HEIGHT,
    CONF_TOLERANCE,
    CONF_BLOCKS_CARRIED,
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RvLevelConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    async_add_entities(RvLevelNumber(entry, key) for key in KEYS)


class RvLevelNumber(RvLevelEntity, NumberEntity):
    _attr_mode = NumberMode.BOX

    def __init__(self, entry: RvLevelConfigEntry, key: str) -> None:
        super().__init__(entry, key)
        self.key = key
        unit = entry.data[CONF_UNIT]
        spec = SETTINGS[unit][key]
        self._attr_native_min_value = spec.min
        self._attr_native_max_value = spec.max
        self._attr_native_step = spec.step
        if key != CONF_BLOCKS_CARRIED:
            self._attr_native_unit_of_measurement = unit

    @property
    def native_value(self) -> float:
        return self.entry.options[self.key]

    async def async_set_native_value(self, value: float) -> None:
        self.hass.config_entries.async_update_entry(
            self.entry, options={**self.entry.options, self.key: value}
        )
