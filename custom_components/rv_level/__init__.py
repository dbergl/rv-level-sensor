from functools import partial

from homeassistant.const import Platform
from homeassistant.core import CoreState, HomeAssistant
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.start import async_at_started

from .const import DOMAIN
from .dashboard import async_remove_dashboard, async_setup_dashboard
from .migrate import ISSUE_YAML_PACKAGE_LOADED, async_migrate_ids
from .runtime import RvLevelConfigEntry, RvLevelRuntime

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.NUMBER]


async def async_setup_entry(hass: HomeAssistant, entry: RvLevelConfigEntry) -> bool:
    runtime = RvLevelRuntime(hass, entry)
    await runtime.async_start()
    entry.async_on_unload(runtime.async_stop)
    entry.runtime_data = runtime

    await async_migrate_ids(hass, entry)
    if hass.state is not CoreState.running:
        # Entries left undecided at startup are settled once every integration is up.
        entry.async_on_unload(async_at_started(hass, partial(async_migrate_ids, entry=entry)))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    await async_setup_dashboard(hass)

    # Options and number changes recompute in place; no reload.
    entry.async_on_unload(entry.add_update_listener(_options_updated))
    return True


async def _options_updated(hass: HomeAssistant, entry: RvLevelConfigEntry) -> None:
    await entry.runtime_data.async_recompute()


async def async_unload_entry(hass: HomeAssistant, entry: RvLevelConfigEntry) -> bool:
    if not await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        return False
    await async_remove_dashboard(hass)
    ir.async_delete_issue(hass, DOMAIN, ISSUE_YAML_PACKAGE_LOADED)
    return True
