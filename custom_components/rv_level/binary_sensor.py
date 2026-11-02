from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import AcEntity
from .ac_protocol import AcStatus
from .ac_runtime import AcConfigEntry
from .const import KIND_AC, entry_kind
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


@dataclass(frozen=True, kw_only=True)
class AcBinarySensorDescription(BinarySensorEntityDescription):
    value_fn: Callable[[AcStatus], bool]


AC_DESCRIPTIONS: tuple[AcBinarySensorDescription, ...] = (
    AcBinarySensorDescription(
        key="power",
        translation_key="power",
        device_class=BinarySensorDeviceClass.POWER,
        value_fn=lambda s: s.power,
    ),
    AcBinarySensorDescription(
        key="unit_in_celsius",
        translation_key="unit_in_celsius",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda s: s.celsius,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RvLevelConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    if entry_kind(entry.data) == KIND_AC:
        async_add_entities(
            [*(AcBinarySensor(entry, d) for d in AC_DESCRIPTIONS), AcResponding(entry)]
        )
        return
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


class AcBinarySensor(AcEntity, BinarySensorEntity):
    entity_description: AcBinarySensorDescription

    def __init__(self, entry: AcConfigEntry, description: AcBinarySensorDescription) -> None:
        super().__init__(entry, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value_fn(self.status) if self.status else None


class AcResponding(AcEntity, BinarySensorEntity):
    """Whether HA hears the unit now; available even while the link is down, to say so."""

    _attr_translation_key = "ac_responding"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(self, entry: AcConfigEntry) -> None:
        super().__init__(entry, "ac_responding")

    @property
    def available(self) -> bool:
        return True

    @property
    def is_on(self) -> bool:
        return self.runtime.available and self.status is not None
