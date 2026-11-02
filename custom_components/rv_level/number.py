from homeassistant.components.number import NumberDeviceClass, NumberEntity, NumberMode
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import AcEntity
from .ac_protocol import Op
from .ac_runtime import AcConfigEntry
from .const import (
    CONF_BLOCK_HEIGHT,
    CONF_BLOCKS_CARRIED,
    CONF_TOLERANCE,
    CONF_TRACK,
    CONF_UNIT,
    CONF_WHEELBASE,
    KIND_AC,
    SETTINGS,
    entry_kind,
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
    if entry_kind(entry.data) == KIND_AC:
        async_add_entities([AcSetpoint(entry)])
        return
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


class AcSetpoint(AcEntity, NumberEntity):
    # The firmware reports the setpoint in °F even while the unit shows °C.
    _attr_translation_key = "setpoint"
    _attr_device_class = NumberDeviceClass.TEMPERATURE
    _attr_native_unit_of_measurement = UnitOfTemperature.FAHRENHEIT
    _attr_native_min_value = 61
    _attr_native_max_value = 86
    _attr_native_step = 1

    def __init__(self, entry: AcConfigEntry) -> None:
        super().__init__(entry, "setpoint")

    @property
    def native_value(self) -> int | None:
        return self.status.setpoint_f if self.status else None

    async def async_set_native_value(self, value: float) -> None:
        await self._command(Op.Setpoint, round(value))
