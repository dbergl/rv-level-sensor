from homeassistant.const import Platform
from homeassistant.core import HomeAssistant

from .dashboard import async_remove_dashboard, async_setup_dashboard
from .migrate import async_migrate_ids
from .runtime import RvLevelConfigEntry, RvLevelRuntime

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.NUMBER]


async def async_setup_entry(hass: HomeAssistant, entry: RvLevelConfigEntry) -> bool:
    runtime = RvLevelRuntime(hass, entry)
    await runtime.async_start()
    entry.runtime_data = runtime

    await async_migrate_ids(hass, entry)
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
    await entry.runtime_data.async_stop()
    await async_remove_dashboard(hass)
    return True
