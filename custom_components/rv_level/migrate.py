import re

from homeassistant.core import HomeAssistant
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import issue_registry as ir

from .binary_sensor import DESCRIPTIONS as BINARY_SENSOR_DESCRIPTIONS
from .const import DOMAIN
from .runtime import RvLevelConfigEntry
from .sensor import DESCRIPTIONS as SENSOR_DESCRIPTIONS

ISSUE_YAML_PACKAGE_LOADED = "yaml_package_loaded"

# Entity key -> the entity_id the YAML package used, kept so dashboards and
# automations carry over.
WANTED_IDS: dict[str, str] = {
    **{d.key: f"sensor.rv_level_{d.key}" for d in SENSOR_DESCRIPTIONS},
    **{d.key: f"binary_sensor.rv_level_{d.key}" for d in BINARY_SENSOR_DESCRIPTIONS},
}


def _provided(hass: HomeAssistant, entity_id: str) -> bool:
    state = hass.states.get(entity_id)
    return state is not None and not state.attributes.get("restored")


async def async_migrate_ids(hass: HomeAssistant, entry: RvLevelConfigEntry) -> bool:
    """Take over the YAML package's entity IDs; return True if it is still loaded."""
    registry = er.async_get(hass)
    package_loaded = False

    for key, wanted in WANTED_IDS.items():
        existing = registry.async_get(wanted)
        if (
            existing is None
            or existing.platform != "template"
            or existing.unique_id != f"{DOMAIN}_{key}"
        ):
            continue
        if _provided(hass, wanted):
            package_loaded = True
        else:
            registry.async_remove(wanted)

    for reg_entry in er.async_entries_for_config_entry(registry, entry.entry_id):
        key = reg_entry.unique_id.removeprefix(f"{entry.entry_id}_")
        wanted = WANTED_IDS.get(key)
        # Only undo the numeric suffix HA adds on a collision, not user renames.
        if (
            wanted is not None
            and re.fullmatch(rf"{re.escape(wanted)}_\d+", reg_entry.entity_id)
            and registry.async_get(wanted) is None
        ):
            registry.async_update_entity(reg_entry.entity_id, new_entity_id=wanted)

    if package_loaded:
        ir.async_create_issue(
            hass,
            DOMAIN,
            ISSUE_YAML_PACKAGE_LOADED,
            is_fixable=False,
            severity=ir.IssueSeverity.WARNING,
            translation_key=ISSUE_YAML_PACKAGE_LOADED,
        )
    else:
        ir.async_delete_issue(hass, DOMAIN, ISSUE_YAML_PACKAGE_LOADED)
    return package_loaded
