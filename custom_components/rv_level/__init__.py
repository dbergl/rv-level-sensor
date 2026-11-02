from functools import partial

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import CoreState, HomeAssistant
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.start import async_at_started

from .ac_runtime import AcConfigEntry, AcRuntime
from .const import CONF_ADDRESS, CONF_KEY, DOMAIN, KIND_AC, entry_kind
from .dashboard import async_remove_dashboard, async_setup_dashboard
from .migrate import ISSUE_YAML_PACKAGE_LOADED, async_migrate_ids
from .migrate import async_migrate_entry as async_migrate_entry  # HA looks it up here
from .runtime import RvLevelConfigEntry, RvLevelRuntime

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.NUMBER]
AC_PLATFORMS = [
    Platform.CLIMATE,
    Platform.SWITCH,
    Platform.SELECT,
    Platform.NUMBER,
    Platform.SENSOR,
    Platform.BINARY_SENSOR,
]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    if entry_kind(entry.data) == KIND_AC:
        return await _async_setup_ac(hass, entry)

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


async def _async_setup_ac(hass: HomeAssistant, entry: AcConfigEntry) -> bool:
    # Setup succeeds with the AC bridge out of range; the runtime keeps retrying.
    runtime = AcRuntime(hass, entry.data[CONF_ADDRESS], bytes.fromhex(entry.data[CONF_KEY]))
    await runtime.async_start()
    entry.async_on_unload(runtime.async_stop)
    entry.runtime_data = runtime
    await hass.config_entries.async_forward_entry_setups(entry, AC_PLATFORMS)
    return True


async def _options_updated(hass: HomeAssistant, entry: RvLevelConfigEntry) -> None:
    await entry.runtime_data.async_recompute()


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    if entry_kind(entry.data) == KIND_AC:
        return await hass.config_entries.async_unload_platforms(entry, AC_PLATFORMS)

    if not await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        return False
    await async_remove_dashboard(hass)
    ir.async_delete_issue(hass, DOMAIN, ISSUE_YAML_PACKAGE_LOADED)
    return True
