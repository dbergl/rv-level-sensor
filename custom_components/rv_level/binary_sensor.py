from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .entity import RvLevelEntity
from .runtime import RvLevelConfigEntry, Snapshot


@dataclass(frozen=True, kw_only=True)
class RvLevelBinarySensorDescription(BinarySensorEntityDescription):
    value_fn: Callable[[Snapshot], bool | None]


DESCRIPTIONS: tuple[RvLevelBinarySensorDescription, ...] = (
    RvLevelBinarySensorDescription(
        key="level", translation_key="level", value_fn=lambda s: s.level
    ),
    RvLevelBinarySensorDescription(
        key="moving",
        translation_key="moving",
        device_class=BinarySensorDeviceClass.MOVING,
        value_fn=lambda s: s.moving,
    ),
    RvLevelBinarySensorDescription(
        key="problem",
        translation_key="problem",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda s: s.problem,
    ),
    RvLevelBinarySensorDescription(
        key="short_on_blocks",
        translation_key="short_on_blocks",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda s: s.short,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RvLevelConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    async_add_entities(RvLevelBinarySensor(entry, d) for d in DESCRIPTIONS)


class RvLevelBinarySensor(RvLevelEntity, BinarySensorEntity):
    entity_description: RvLevelBinarySensorDescription

    def __init__(
        self, entry: RvLevelConfigEntry, description: RvLevelBinarySensorDescription
    ) -> None:
        super().__init__(entry, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value_fn(self.runtime.snapshot)

    @property
    def available(self) -> bool:
        return self.is_on is not None
