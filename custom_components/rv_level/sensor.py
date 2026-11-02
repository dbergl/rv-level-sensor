from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import (
    DEGREE,
    EntityCategory,
    UnitOfElectricPotential,
    UnitOfTemperature,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .ac_entity import MODE_OPTIONS, AcEntity
from .ac_protocol import AcStatus
from .ac_runtime import AcConfigEntry
from .const import CONF_UNIT, KIND_AC, entry_kind
from .entity import RvLevelEntity
from .leveling import CORNERS
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


@dataclass(frozen=True, kw_only=True)
class AcSensorDescription(SensorEntityDescription):
    value_fn: Callable[[AcStatus], float | int | str | None]


AC_DESCRIPTIONS: tuple[AcSensorDescription, ...] = (
    AcSensorDescription(
        key="mode",
        translation_key="mode",
        device_class=SensorDeviceClass.ENUM,
        options=list(MODE_OPTIONS.values()),
        value_fn=lambda s: MODE_OPTIONS.get(s.mode),
    ),
    AcSensorDescription(
        key="air_temperature",
        translation_key="air_temperature",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.FAHRENHEIT,
        state_class=SensorStateClass.MEASUREMENT,
        value_fn=lambda s: s.air_temp_f,
    ),
    AcSensorDescription(
        key="battery_voltage",
        translation_key="battery_voltage",
        device_class=SensorDeviceClass.VOLTAGE,
        native_unit_of_measurement=UnitOfElectricPotential.VOLT,
        state_class=SensorStateClass.MEASUREMENT,
        value_fn=lambda s: s.battery_v,
    ),
    AcSensorDescription(
        key="low_voltage_protection",
        translation_key="low_voltage_protection",
        device_class=SensorDeviceClass.VOLTAGE,
        native_unit_of_measurement=UnitOfElectricPotential.VOLT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda s: s.low_voltage_protection,
    ),
    AcSensorDescription(
        key="status_frame",
        translation_key="status_frame",
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda s: s.raw.hex(" "),
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: RvLevelConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    if entry_kind(entry.data) == KIND_AC:
        async_add_entities(AcSensor(entry, d) for d in AC_DESCRIPTIONS)
        return
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


class AcSensor(AcEntity, SensorEntity):
    entity_description: AcSensorDescription

    def __init__(self, entry: AcConfigEntry, description: AcSensorDescription) -> None:
        super().__init__(entry, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> float | int | str | None:
        return self.entity_description.value_fn(self.status) if self.status else None
