"""The RV Level dashboard, served from the integration as a YAML-mode Lovelace dashboard."""

import logging
from pathlib import Path
from typing import Any

from homeassistant.components import frontend
from homeassistant.components.http import StaticPathConfig
from homeassistant.components.lovelace.const import LOVELACE_DATA
from homeassistant.components.lovelace.dashboard import LovelaceConfig
from homeassistant.core import HomeAssistant
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.json import json_bytes, json_fragment
from homeassistant.util.yaml import load_yaml_dict

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

URL_PATH = "rv-level"
IMAGE_URL = "/rv_level/van_top.svg"
TITLE = "RV Level"
ICON = "mdi:spirit-level"

_HERE = Path(__file__).parent
_DATA_STATIC_REGISTERED = f"{DOMAIN}_static_registered"

ISSUE_MUSHROOM_MISSING = "mushroom_missing"
ISSUE_DASHBOARD_URL_TAKEN = "dashboard_url_taken"
ISSUE_DASHBOARD_FAILED = "dashboard_failed"
_ISSUES = (ISSUE_MUSHROOM_MISSING, ISSUE_DASHBOARD_URL_TAKEN, ISSUE_DASHBOARD_FAILED)


class RvLevelDashboard(LovelaceConfig):
    """The dashboard shipped in dashboard.yaml; read-only, like a YAML dashboard."""

    def __init__(self, hass: HomeAssistant, url_path: str | None, config: dict[str, Any] | None) -> None:
        super().__init__(hass, url_path, config)
        self._config: dict[str, Any] | None = None

    @property
    def mode(self) -> str:
        return "yaml"

    async def async_get_info(self) -> dict[str, Any]:
        config = await self.async_load(False)
        return {"mode": self.mode, "views": len(config["views"])}

    async def async_load(self, force: bool) -> dict[str, Any]:
        # The file ships with the integration and only changes on upgrade, so it is read once.
        if self._config is None:
            self._config = await self.hass.async_add_executor_job(load_yaml_dict, _HERE / "dashboard.yaml")
        return self._config

    async def async_json(self, force: bool) -> json_fragment:
        return json_fragment(json_bytes(await self.async_load(force)))


def _issue(hass: HomeAssistant, issue_id: str) -> None:
    ir.async_create_issue(
        hass, DOMAIN, issue_id, is_fixable=False, severity=ir.IssueSeverity.WARNING, translation_key=issue_id
    )


async def async_setup_dashboard(hass: HomeAssistant) -> None:
    """Add the dashboard to the sidebar; problems raise a repair issue, never an error."""
    try:
        # Static routes can't be removed, so register the image once per HA run.
        if not hass.data.get(_DATA_STATIC_REGISTERED):
            await hass.http.async_register_static_paths(
                [StaticPathConfig(IMAGE_URL, str(_HERE / "van_top.svg"), True)]
            )
            hass.data[_DATA_STATIC_REGISTERED] = True

        dashboards = hass.data[LOVELACE_DATA].dashboards
        existing = dashboards.get(URL_PATH)
        if existing is not None and not isinstance(existing, RvLevelDashboard):
            _issue(hass, ISSUE_DASHBOARD_URL_TAKEN)
            return
        frontend.async_register_built_in_panel(
            hass,
            "lovelace",
            frontend_url_path=URL_PATH,
            sidebar_title=TITLE,
            sidebar_icon=ICON,
            config={"mode": "yaml"},
            require_admin=False,
            update=True,
        )
        dashboards[URL_PATH] = RvLevelDashboard(
            hass,
            URL_PATH,
            {"mode": "yaml", "title": TITLE, "icon": ICON, "show_in_sidebar": True, "require_admin": False},
        )
    except Exception:
        _LOGGER.warning("Could not add the RV Level dashboard", exc_info=True)
        _issue(hass, ISSUE_DASHBOARD_FAILED)
        return

    await async_check_mushroom(hass)


async def async_remove_dashboard(hass: HomeAssistant) -> None:
    """Take the dashboard out of the sidebar and clear its issues."""
    dashboards = hass.data[LOVELACE_DATA].dashboards
    if isinstance(dashboards.get(URL_PATH), RvLevelDashboard):
        dashboards.pop(URL_PATH)
        frontend.async_remove_panel(hass, URL_PATH)
    for issue_id in _ISSUES:
        ir.async_delete_issue(hass, DOMAIN, issue_id)


async def async_check_mushroom(hass: HomeAssistant) -> bool | None:
    """Return whether a Mushroom resource is installed, or None if the resources can't be listed."""
    resources = hass.data[LOVELACE_DATA].resources
    try:
        # Storage-mode resources load lazily; this loads them, as the frontend's first request would.
        await resources.async_get_info()
        found = any("mushroom" in item.get("url", "") for item in resources.async_items())
    except Exception:
        _LOGGER.debug("Could not list Lovelace resources to look for Mushroom", exc_info=True)
        return None

    if found:
        ir.async_delete_issue(hass, DOMAIN, ISSUE_MUSHROOM_MISSING)
    else:
        _issue(hass, ISSUE_MUSHROOM_MISSING)
    return found
