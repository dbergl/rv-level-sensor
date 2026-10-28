import pytest
from homeassistant.helpers import device_registry as dr, entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.source import ROLE_SUFFIXES


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    """Let Home Assistant load custom_components/ in every test."""


ALL_ROLES = ("pitch", "roll", "level", "moving", "problem")
DEFAULT_ENTITY_IDS = {
    "pitch": "sensor.rv_level_3032_rotation",
    "roll": "sensor.rv_level_3032_rotation_2",
    "level": "binary_sensor.rv_level_3032_generic",
    "moving": "binary_sensor.rv_level_3032_moving",
    "problem": "binary_sensor.rv_level_3032_problem",
}


@pytest.fixture
def bthome_device(hass):
    """Return a factory that registers a BTHome device and returns its device_id."""

    def make(
        address="02:00:00:00:30:32",
        name="RV Level 3032",
        roles=ALL_ROLES,
        entity_ids=None,
    ):
        entry = MockConfigEntry(domain="bthome", unique_id=address)
        entry.add_to_hass(hass)
        device = dr.async_get(hass).async_get_or_create(
            config_entry_id=entry.entry_id,
            connections={("bluetooth", address)},
            name=name,
        )
        ids = {**DEFAULT_ENTITY_IDS, **(entity_ids or {})}
        for role in roles:
            entity_id = ids[role]
            er.async_get(hass).async_get_or_create(
                entity_id.split(".")[0],
                "bthome",
                f"{address}-{ROLE_SUFFIXES[role]}",
                suggested_object_id=entity_id.split(".")[1],
                config_entry=entry,
                device_id=device.id,
            )
        return device.id

    return make
