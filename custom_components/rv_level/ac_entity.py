"""Base for the CountryModPro entities on an AC entry."""

from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity import Entity

from .ac_protocol import AcStatus, Mode, Op
from .ac_runtime import AcConfigEntry
from .const import DOMAIN

# Select and sensor option for each mode HA offers; Dehumid and Unknown show as unknown.
MODE_OPTIONS: dict[Mode, str] = {
    Mode.Cool: "cool",
    Mode.Eco: "eco",
    Mode.Sleep: "sleep",
    Mode.Turbo: "turbo",
    Mode.FanOnly: "fan_only",
}
PRESETS = (Mode.Eco, Mode.Sleep, Mode.Turbo)
FAN_OPTIONS = ["auto", "1", "2", "3", "4", "5"]


def fan_option(fan: int | None) -> str | None:
    return FAN_OPTIONS[fan] if fan is not None and 0 <= fan < len(FAN_OPTIONS) else None


def in_preset(status: AcStatus | None) -> bool:
    return bool(status and status.power and status.mode in PRESETS)


def mode_command(status: AcStatus | None, mode: Mode) -> tuple[Op, int]:
    """Mode Cool leaves a preset alone on the unit, so leaving one is ClearPreset."""
    if mode is Mode.Cool and in_preset(status):
        return Op.ClearPreset, 0
    return Op.Mode, int(mode)


class AcEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, entry: AcConfigEntry, key: str) -> None:
        self.runtime = entry.runtime_data
        self._attr_unique_id = f"{entry.entry_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name="CountryModPro",
            manufacturer="CountryModPro",
            model="Rooftop AC",
        )

    @property
    def available(self) -> bool:
        return self.runtime.available

    @property
    def status(self) -> AcStatus | None:
        """The unit's report, or None while it isn't responding."""
        status = self.runtime.status
        return status if status is not None and status.responding else None

    @property
    def running(self) -> AcStatus | None:
        """The status while on; the mode and fan reported while off aren't trusted."""
        status = self.status
        return status if status is not None and status.power else None

    async def _command(self, op: Op, value: int) -> None:
        # No optimistic state: the next status from the unit is the truth.
        await self.runtime.command(op, int(value))

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(self.runtime.async_add_listener(self.async_write_ha_state))
