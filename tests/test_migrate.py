from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import issue_registry as ir
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.const import DOMAIN
from custom_components.rv_level.migrate import ISSUE_YAML_PACKAGE_LOADED

from conftest import _setup

WANTED = "sensor.rv_level_lift_fl"


def _template_entry(hass, unique_id="rv_level_lift_fl"):
    return er.async_get(hass).async_get_or_create(
        "sensor", "template", unique_id, suggested_object_id="rv_level_lift_fl"
    )


def _integration_entity(hass, entry):
    registry = er.async_get(hass)
    return registry.async_get_entity_id("sensor", DOMAIN, f"{entry.entry_id}_lift_fl")


def _issue(hass):
    return ir.async_get(hass).async_get_issue(DOMAIN, ISSUE_YAML_PACKAGE_LOADED)


async def test_removes_orphaned_template_entries(hass, bthome_device):
    _template_entry(hass)
    hass.states.async_set(WANTED, "unavailable", {"restored": True})

    entry = await _setup(hass, bthome_device)

    assert _integration_entity(hass, entry) == WANTED
    assert er.async_get(hass).async_get(WANTED).platform == DOMAIN
    assert _issue(hass) is None


async def test_package_still_loaded_raises_issue(hass, bthome_device):
    _template_entry(hass)
    hass.states.async_set(WANTED, "1.0")

    entry = await _setup(hass, bthome_device)

    assert er.async_get(hass).async_get(WANTED).platform == "template"
    assert _integration_entity(hass, entry) == f"{WANTED}_2"
    issue = _issue(hass)
    assert issue is not None
    assert not issue.is_fixable
    assert issue.severity == ir.IssueSeverity.WARNING


async def test_suffixed_ids_renamed_after_package_removed(hass, bthome_device):
    _template_entry(hass)
    hass.states.async_set(WANTED, "1.0")
    entry = await _setup(hass, bthome_device)
    assert _integration_entity(hass, entry) == f"{WANTED}_2"

    # Simulate the restart after the package is removed.
    hass.states.async_remove(WANTED)
    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()

    assert _integration_entity(hass, entry) == WANTED
    assert _issue(hass) is None


async def test_leaves_unrelated_template_entities(hass, bthome_device):
    _template_entry(hass, unique_id="something_else")

    entry = await _setup(hass, bthome_device)

    assert er.async_get(hass).async_get(WANTED).unique_id == "something_else"
    assert _integration_entity(hass, entry) == f"{WANTED}_2"
    assert _issue(hass) is None


async def test_user_rename_kept(hass, bthome_device):
    entry = await _setup(hass, bthome_device)
    er.async_get(hass).async_update_entity(WANTED, new_entity_id="sensor.front_left_lift")

    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()

    assert _integration_entity(hass, entry) == "sensor.front_left_lift"
