import pytest
from homeassistant.const import STATE_OFF, STATE_ON
from homeassistant.helpers import device_registry as dr, entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.const import DOMAIN
from custom_components.rv_level.source import ROLE_SUFFIXES


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    """Let Home Assistant load custom_components/ in every test."""


@pytest.fixture(autouse=True)
def no_deprecation_reports(caplog):
    """Fail on HA's warnings that the integration uses a deprecated API."""
    yield
    reports = [
        r.getMessage()
        for when in ("setup", "call")
        for r in caplog.get_records(when)
        if r.name == "homeassistant.helpers.frame" and "rv_level" in r.getMessage()
    ]
    assert not reports, reports[0]


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


US_OPTIONS = {
    "wheelbase": 159,
    "track": 68.5,
    "driver_side": "left",
    "block_height": 1.0,
    "tolerance": 0.5,
    "blocks_carried": 10,
}
CM_OPTIONS = {
    "wheelbase": 404,
    "track": 174,
    "driver_side": "left",
    "block_height": 2.5,
    "tolerance": 1.25,
    "blocks_carried": 10,
}


def _set_sources(hass, pitch="0", roll="0", level=STATE_ON, moving=STATE_OFF, problem=STATE_OFF):
    for role, value in (
        ("pitch", pitch),
        ("roll", roll),
        ("level", level),
        ("moving", moving),
        ("problem", problem),
    ):
        hass.states.async_set(DEFAULT_ENTITY_IDS[role], value)


async def _setup(
    hass, bthome_device, unit="in", options=None, roles=None, expect_loaded=True, **sources
):
    device_id = bthome_device(**({"roles": roles} if roles else {}))
    entry = MockConfigEntry(
        domain=DOMAIN,
        unique_id="02:00:00:00:30:32",
        data={"device_id": device_id, "address": "02:00:00:00:30:32", "unit": unit},
        options=options or (US_OPTIONS if unit == "in" else CM_OPTIONS),
    )
    entry.add_to_hass(hass)
    _set_sources(hass, **sources)
    assert await hass.config_entries.async_setup(entry.entry_id) is expect_loaded
    await hass.async_block_till_done()
    return entry
