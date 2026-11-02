from typing import Any

from homeassistant.components.climate import (
    ATTR_TEMPERATURE,
    PRESET_BOOST,
    PRESET_ECO,
    PRESET_NONE,
    PRESET_SLEEP,
    ClimateEntity,
    ClimateEntityFeature,
    HVACMode,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import FAN_OPTIONS, AcEntity, fan_option
from .ac_protocol import Mode, Op
from .ac_runtime import AcConfigEntry
from .const import KIND_AC, entry_kind

PRESET_MODES: dict[str, Mode] = {
    PRESET_ECO: Mode.Eco,
    PRESET_SLEEP: Mode.Sleep,
    PRESET_BOOST: Mode.Turbo,
}
MODE_PRESETS = {mode: preset for preset, mode in PRESET_MODES.items()}
MODE_HVAC = {
    Mode.Cool: HVACMode.COOL,
    Mode.Eco: HVACMode.COOL,
    Mode.Sleep: HVACMode.COOL,
    Mode.Turbo: HVACMode.COOL,
    Mode.FanOnly: HVACMode.FAN_ONLY,
}


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    if entry_kind(entry.data) != KIND_AC:
        return
    async_add_entities([AcClimate(entry)])


class AcClimate(AcEntity, ClimateEntity):
    # No hvac_action: the unit doesn't report compressor state.
    _attr_name = None
    _attr_hvac_modes = [HVACMode.OFF, HVACMode.COOL, HVACMode.FAN_ONLY]
    _attr_preset_modes = [PRESET_NONE, *PRESET_MODES]
    _attr_fan_modes = FAN_OPTIONS
    _attr_min_temp = 61
    _attr_max_temp = 86
    _attr_target_temperature_step = 1
    _attr_temperature_unit = UnitOfTemperature.FAHRENHEIT
    _attr_supported_features = (
        ClimateEntityFeature.TARGET_TEMPERATURE
        | ClimateEntityFeature.FAN_MODE
        | ClimateEntityFeature.PRESET_MODE
        | ClimateEntityFeature.TURN_ON
        | ClimateEntityFeature.TURN_OFF
    )

    def __init__(self, entry: AcConfigEntry) -> None:
        super().__init__(entry, "climate")

    @property
    def hvac_mode(self) -> HVACMode | None:
        if (s := self.status) is None:
            return None
        if not s.power:
            return HVACMode.OFF
        return MODE_HVAC.get(s.mode)  # Dehumid/Unknown: unknown, not a guess

    @property
    def preset_mode(self) -> str | None:
        if (s := self.status) is None or s.mode not in MODE_HVAC:
            return None
        return MODE_PRESETS.get(s.mode, PRESET_NONE)

    @property
    def fan_mode(self) -> str | None:
        return fan_option(self.status.fan) if self.status else None

    @property
    def target_temperature(self) -> int | None:
        return self.status.setpoint_f if self.status else None

    @property
    def current_temperature(self) -> int | None:
        return self.status.air_temp_f if self.status else None

    async def async_set_hvac_mode(self, hvac_mode: HVACMode) -> None:
        if hvac_mode == HVACMode.OFF:
            await self._command(Op.Power, 0)
        elif hvac_mode == HVACMode.FAN_ONLY:
            await self._command(Op.Mode, Mode.FanOnly)
        else:
            # Already cooling in a preset is a no-op on the AC bridge; the preset stays.
            await self._command(Op.Mode, Mode.Cool)

    async def async_turn_on(self) -> None:
        await self._command(Op.Power, 1)

    async def async_turn_off(self) -> None:
        await self._command(Op.Power, 0)

    async def async_set_preset_mode(self, preset_mode: str) -> None:
        if preset_mode == PRESET_NONE:
            await self._command(Op.ClearPreset, 0)
        else:
            await self._command(Op.Mode, PRESET_MODES[preset_mode])

    async def async_set_fan_mode(self, fan_mode: str) -> None:
        await self._command(Op.Fan, FAN_OPTIONS.index(fan_mode))

    async def async_set_temperature(self, **kwargs: Any) -> None:
        if (temperature := kwargs.get(ATTR_TEMPERATURE)) is not None:
            await self._command(Op.Setpoint, round(temperature))
