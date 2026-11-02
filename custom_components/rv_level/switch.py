from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.switch import SwitchEntity, SwitchEntityDescription
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import AcEntity
from .ac_protocol import AcStatus, Op
from .ac_runtime import AcConfigEntry
from .const import KIND_AC, entry_kind


@dataclass(frozen=True, kw_only=True)
class AcSwitchDescription(SwitchEntityDescription):
    op: Op
    value_fn: Callable[[AcStatus], bool]


DESCRIPTIONS: tuple[AcSwitchDescription, ...] = (
    AcSwitchDescription(
        key="power", translation_key="power", op=Op.Power, value_fn=lambda s: s.power
    ),
    AcSwitchDescription(
        key="display_in_celsius",
        translation_key="display_in_celsius",
        op=Op.Celsius,
        value_fn=lambda s: s.celsius,
    ),
    AcSwitchDescription(
        key="light", translation_key="light", op=Op.Light, value_fn=lambda s: s.light
    ),
    AcSwitchDescription(
        key="display",
        translation_key="display",
        op=Op.Display,
        value_fn=lambda s: s.display,
    ),
    AcSwitchDescription(
        key="vent_oscillation",
        translation_key="vent_oscillation",
        op=Op.Oscillate,
        value_fn=lambda s: s.oscillating,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    if entry_kind(entry.data) != KIND_AC:
        return
    async_add_entities(AcSwitch(entry, d) for d in DESCRIPTIONS)


class AcSwitch(AcEntity, SwitchEntity):
    entity_description: AcSwitchDescription

    def __init__(self, entry: AcConfigEntry, description: AcSwitchDescription) -> None:
        super().__init__(entry, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value_fn(self.status) if self.status else None

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self._command(self.entity_description.op, 1)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self._command(self.entity_description.op, 0)
