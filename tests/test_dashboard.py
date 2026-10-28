import re
from pathlib import Path
from unittest.mock import patch

import yaml
from homeassistant.components import frontend
from homeassistant.components.lovelace.const import LOVELACE_DATA
from homeassistant.components.lovelace.dashboard import LovelaceYAML
from homeassistant.config_entries import ConfigEntryState
from homeassistant.helpers import issue_registry as ir
from homeassistant.setup import async_setup_component
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.const import DOMAIN
from custom_components.rv_level.dashboard import RvLevelDashboard

from conftest import US_OPTIONS, _set_sources, _setup

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = ROOT / "custom_components" / "rv_level" / "dashboard.yaml"
YAML_DASHBOARD = ROOT / "homeassistant" / "dashboards" / "rv_level.yaml"


def _issue(hass, issue_id):
    return ir.async_get(hass).async_get_issue(DOMAIN, issue_id)


def _to_integration(text):
    """The YAML package's dashboard, as the integration serves it."""
    text = text.replace("image: /local/rv_level/van_top.svg", "image: /rv_level/van_top.svg")
    text = re.sub(
        r"\"\{\{ states\('(sensor\.rv_level_lift_\w+)'\) \| float\(0\) \| round\(1\) \}\} in\"",
        lambda m: (
            f"\"{{{{ states('{m[1]}') | float(0) | round(1) }}}} "
            f"{{{{ state_attr('{m[1]}', 'unit_of_measurement') }}}}\""
        ),
        text,
    )
    return text.replace("input_number.rv_level_", "number.rv_level_")


async def test_dashboard_registered(hass, bthome_device):
    await _setup(hass, bthome_device)

    d = hass.data[LOVELACE_DATA].dashboards["rv-level"]
    assert isinstance(d, RvLevelDashboard)
    assert "rv-level" in hass.data[frontend.DATA_PANELS]
    assert (await d.async_load(False))["views"][0]["path"] == "rv-level"


async def test_dashboard_entities_exist(hass, bthome_device):
    await _setup(hass, bthome_device)

    used = set(re.findall(r"\b(?:sensor|binary_sensor|number)\.rv_level_[a-z_]+", DASHBOARD.read_text()))
    assert used, "dashboard references no entities"
    assert {e for e in used if hass.states.get(e) is None} == set()


def test_dashboard_matches_yaml_version():
    assert DASHBOARD.read_text() == _to_integration(YAML_DASHBOARD.read_text())


def test_corner_cards_hidden_on_problem():
    view = yaml.safe_load(DASHBOARD.read_text())["views"][0]
    pe = view["sections"][0]["cards"][0]
    assert pe["type"] == "picture-elements"
    corner_groups = [
        el for el in pe["elements"]
        if el["type"] == "conditional"
        and any(e.get("entity", "").startswith("sensor.rv_level_blocks_") for e in el["elements"])
    ]
    assert len(corner_groups) == 1
    assert {"entity": "binary_sensor.rv_level_problem", "state": "off"} in corner_groups[0]["conditions"]
    corners = [e["entity"] for e in corner_groups[0]["elements"]]
    assert sorted(corners) == [f"sensor.rv_level_blocks_{c}" for c in ("fl", "fr", "rl", "rr")]


async def test_url_taken_raises_issue(hass, bthome_device):
    assert await async_setup_component(hass, "lovelace", {})
    other = LovelaceYAML(hass, "rv-level", {"mode": "yaml", "filename": "dashboards/rv_level.yaml"})
    hass.data[LOVELACE_DATA].dashboards["rv-level"] = other

    entry = await _setup(hass, bthome_device)

    assert entry.state is ConfigEntryState.LOADED
    assert hass.data[LOVELACE_DATA].dashboards["rv-level"] is other
    assert _issue(hass, "dashboard_url_taken") is not None

    assert await hass.config_entries.async_unload(entry.entry_id)
    assert hass.data[LOVELACE_DATA].dashboards["rv-level"] is other


async def test_registration_failure_raises_issue(hass, bthome_device):
    # Lovelace registers its own panel through the same function, so set it up first.
    assert await async_setup_component(hass, "lovelace", {})
    with patch(
        "custom_components.rv_level.dashboard.frontend.async_register_built_in_panel",
        side_effect=ValueError("boom"),
    ):
        entry = await _setup(hass, bthome_device)

    assert entry.state is ConfigEntryState.LOADED
    assert _issue(hass, "dashboard_failed") is not None
    assert "rv-level" not in hass.data[LOVELACE_DATA].dashboards


async def test_mushroom_missing_issue(hass, bthome_device):
    entry = await _setup(hass, bthome_device)
    assert _issue(hass, "mushroom_missing") is not None

    await hass.data[LOVELACE_DATA].resources.async_create_item(
        {"res_type": "module", "url": "/hacsfiles/lovelace-mushroom/mushroom.js"}
    )
    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()

    assert _issue(hass, "mushroom_missing") is None


async def test_remove_and_readd(hass, bthome_device):
    entry = await _setup(hass, bthome_device)
    device_id = entry.data["device_id"]

    await hass.config_entries.async_remove(entry.entry_id)
    await hass.async_block_till_done()
    assert "rv-level" not in hass.data[LOVELACE_DATA].dashboards
    assert "rv-level" not in hass.data[frontend.DATA_PANELS]
    assert _issue(hass, "mushroom_missing") is None

    entry = MockConfigEntry(
        domain=DOMAIN,
        unique_id="02:00:00:00:30:32",
        data={"device_id": device_id, "address": "02:00:00:00:30:32", "unit": "in"},
        options=US_OPTIONS,
    )
    entry.add_to_hass(hass)
    _set_sources(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()

    assert isinstance(hass.data[LOVELACE_DATA].dashboards["rv-level"], RvLevelDashboard)
    assert "rv-level" in hass.data[frontend.DATA_PANELS]


async def test_image_served(hass, bthome_device, hass_client):
    await _setup(hass, bthome_device)

    client = await hass_client()
    resp = await client.get("/rv_level/van_top.svg")
    assert resp.status == 200
    assert "<svg" in await resp.text()
