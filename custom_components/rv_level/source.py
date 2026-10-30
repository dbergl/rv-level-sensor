"""Locate the BTHome entities that feed the integration."""

from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr, entity_registry as er

# BTHome unique_ids are "<MAC>-<suffix>".
ROLE_SUFFIXES: dict[str, str] = {
    "pitch": "rotation_1",
    "roll": "rotation_2",
    "level": "generic",
    "moving": "moving",
    "problem": "problem",
}

REQUIRED_ROLES = ("pitch", "roll", "problem")


def source_entities(hass: HomeAssistant, device_id: str) -> dict[str, str | None]:
    """Map each role to the device's current entity_id, or None."""
    found: dict[str, str | None] = dict.fromkeys(ROLE_SUFFIXES)
    for entity in er.async_entries_for_device(er.async_get(hass), device_id):
        if entity.platform != "bthome":
            continue
        for role, suffix in ROLE_SUFFIXES.items():
            if entity.unique_id.endswith(f"-{suffix}"):
                found[role] = entity.entity_id
    return found


def find_level_devices(hass: HomeAssistant) -> list[dr.DeviceEntry]:
    """BTHome devices that look like an RV Level sensor."""
    registry = dr.async_get(hass)
    devices = []
    for entry in hass.config_entries.async_entries("bthome"):
        for device in dr.async_entries_for_config_entry(registry, entry.entry_id):
            entities = source_entities(hass, device.id)
            if all(entities[role] for role in REQUIRED_ROLES):
                devices.append(device)
    return devices
