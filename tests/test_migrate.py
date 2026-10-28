from homeassistant.const import EVENT_HOMEASSISTANT_START
from homeassistant.core import CoreState
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import issue_registry as ir
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


async def test_unload_clears_issue(hass, bthome_device):
    _template_entry(hass)
    hass.states.async_set(WANTED, "1.0")
    entry = await _setup(hass, bthome_device)
    assert _issue(hass) is not None

    await hass.config_entries.async_remove(entry.entry_id)
    await hass.async_block_till_done()

    assert _issue(hass) is None


async def test_package_loading_late_at_startup_left_alone(hass, bthome_device):
    hass.set_state(CoreState.not_running)
    _template_entry(hass)

    entry = await _setup(hass, bthome_device)

    # The template integration hasn't added its entity yet; nothing is decided.
    assert er.async_get(hass).async_get(WANTED).platform == "template"
    assert _integration_entity(hass, entry) == f"{WANTED}_2"
    assert _issue(hass) is None

    hass.states.async_set(WANTED, "1.0")
    await hass.async_start()
    await hass.async_block_till_done()

    assert er.async_get(hass).async_get(WANTED).platform == "template"
    assert _integration_entity(hass, entry) == f"{WANTED}_2"
    assert _issue(hass) is not None


async def test_orphan_removed_once_started(hass, bthome_device):
    hass.set_state(CoreState.not_running)
    _template_entry(hass)
    # As the entity registry does at startup for entries nothing provided.
    hass.bus.async_listen_once(
        EVENT_HOMEASSISTANT_START,
        lambda _: hass.states.async_set(WANTED, "unavailable", {"restored": True}),
    )

    entry = await _setup(hass, bthome_device)
    assert er.async_get(hass).async_get(WANTED).platform == "template"
    assert _integration_entity(hass, entry) == f"{WANTED}_2"

    await hass.async_start()
    await hass.async_block_till_done()

    assert _integration_entity(hass, entry) == WANTED
    assert er.async_get(hass).async_get(WANTED).platform == DOMAIN
    assert hass.states.get(WANTED).attributes.get("restored") is None
    assert _issue(hass) is None
