from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import DEGREE
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import CONF_UNIT
from .entity import RvLevelEntity
from .levelling import CORNERS
from .runtime import RvLevelConfigEntry, Snapshot


@dataclass(frozen=True, kw_only=True)
class RvLevelSensorDescription(SensorEntityDescription):
    value_fn: Callable[[Snapshot], float | int | None]


def _angle(key: str) -> RvLevelSensorDescription:
    return RvLevelSensorDescription(
        key=key,
        translation_key=key,
        native_unit_of_measurement=DEGREE,
        state_class=SensorStateClass.MEASUREMENT,
        value_fn=lambda s: getattr(s, key),
    )


DESCRIPTIONS: tuple[RvLevelSensorDescription, ...] = (
    _angle("pitch"),
    _angle("roll"),
    *(
        RvLevelSensorDescription(
            key=f"lift_{corner}",
            translation_key=f"lift_{corner}",
            suggested_display_precision=1,
            value_fn=lambda s, c=corner: s.lifts and s.lifts[c],
        )
        for corner in CORNERS
    ),
    *(
        RvLevelSensorDescription(
            key=f"blocks_{corner}",
            translation_key=f"blocks_{corner}",
            value_fn=lambda s, c=corner: s.layers and s.layers[c],
        )
        for corner in CORNERS
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RvLevelConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    async_add_entities(RvLevelSensor(entry, d) for d in DESCRIPTIONS)


class RvLevelSensor(RvLevelEntity, SensorEntity):
    entity_description: RvLevelSensorDescription

    def __init__(
        self, entry: RvLevelConfigEntry, description: RvLevelSensorDescription
    ) -> None:
        super().__init__(entry, description.key)
        self.entity_description = description
        if description.key.startswith("lift_"):
            self._attr_native_unit_of_measurement = entry.data[CONF_UNIT]

    @property
    def native_value(self) -> float | int | None:
        return self.entity_description.value_fn(self.runtime.snapshot)

    @property
    def available(self) -> bool:
        return self.native_value is not None
