from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import FAN_OPTIONS, MODE_OPTIONS, AcEntity, fan_option, mode_command
from .ac_protocol import Op
from .ac_runtime import AcConfigEntry
from .const import KIND_AC, entry_kind

OPTION_MODES = {option: mode for mode, option in MODE_OPTIONS.items()}


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    if entry_kind(entry.data) != KIND_AC:
        return
    async_add_entities([AcOperatingMode(entry), AcFanLevel(entry)])


class AcOperatingMode(AcEntity, SelectEntity):
    _attr_translation_key = "operating_mode"
    _attr_options = list(MODE_OPTIONS.values())

    def __init__(self, entry: AcConfigEntry) -> None:
        super().__init__(entry, "operating_mode")

    @property
    def current_option(self) -> str | None:
        return MODE_OPTIONS.get(self.running.mode) if self.running else None

    async def async_select_option(self, option: str) -> None:
        await self._command(*mode_command(self.status, OPTION_MODES[option]))


class AcFanLevel(AcEntity, SelectEntity):
    _attr_translation_key = "fan_level"
    _attr_options = FAN_OPTIONS

    def __init__(self, entry: AcConfigEntry) -> None:
        super().__init__(entry, "fan_level")

    @property
    def current_option(self) -> str | None:
        return fan_option(self.running.fan) if self.running else None

    async def async_select_option(self, option: str) -> None:
        await self._command(Op.Fan, FAN_OPTIONS.index(option))
