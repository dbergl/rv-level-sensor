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


def _is_bthome(hass: HomeAssistant, entry_id: str) -> bool:
    entry = hass.config_entries.async_get_entry(entry_id)
    return entry is not None and entry.domain == "bthome"


def find_level_devices(hass: HomeAssistant) -> list[dr.DeviceEntry]:
    """BTHome devices that look like an RV Level sensor."""
    devices = []
    for device in dr.async_get(hass).devices.values():
        if not any(_is_bthome(hass, entry_id) for entry_id in device.config_entries):
            continue
        entities = source_entities(hass, device.id)
        if all(entities[role] for role in REQUIRED_ROLES):
            devices.append(device)
    return devices
