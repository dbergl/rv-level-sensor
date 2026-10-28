"""Every entity the dashboard shows must come from the package, and the corner cards must hide on a fault."""
import re
from pathlib import Path

import yaml

from conftest import HA, package


def entity_id(domain, e):
    """entity_id HA gives a template entity: its name, slugified."""
    return f"{domain}." + re.sub(r"[^a-z0-9]+", "_", e["name"].lower()).strip("_")


def defined_entities():
    pkg = package()
    ids = {f"input_number.{k}" for k in pkg["input_number"]}
    for block in pkg["template"]:
        for domain in ("sensor", "binary_sensor"):
            for e in block.get(domain, []):
                ids.add(entity_id(domain, e))
    return ids


def test_entity_id_comes_from_name_not_unique_id():
    # HA derives a template entity's entity_id from its name; unique_id only ties it to the registry.
    assert entity_id("sensor", {"name": "RV Level Lift FL", "unique_id": "something_else"}) == "sensor.rv_level_lift_fl"
    assert entity_id("binary_sensor", {"name": "RV Level Short On Blocks"}) == "binary_sensor.rv_level_short_on_blocks"


def dashboard_text():
    return (HA / "dashboards" / "rv_level.yaml").read_text()


def test_dashboard_is_valid_yaml():
    assert yaml.safe_load(dashboard_text())["views"]


def test_dashboard_entities_exist():
    used = set(re.findall(r"\b(?:sensor|binary_sensor|input_number)\.rv_level_[a-z_]+", dashboard_text()))
    assert used, "dashboard references no entities"
    assert used <= defined_entities(), used - defined_entities()


def test_dashboard_does_not_reference_ble_entities_directly():
    assert "rv_level_ble_" not in dashboard_text()


def test_corner_cards_hidden_on_problem():
    view = yaml.safe_load(dashboard_text())["views"][0]
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


def test_image_exists():
    assert (HA.parent / "custom_components" / "rv_level" / "van_top.svg").is_file()


def test_package_declares_dashboard():
    # A YAML-mode dashboard: HA adds it to the sidebar, no pasting. url_path needs a hyphen.
    dashboards = package()["lovelace"]["dashboards"]
    assert list(dashboards) == ["rv-level"]
    d = dashboards["rv-level"]
    assert d["mode"] == "yaml"
    assert (HA / d["filename"]).is_file()


def test_dashboard_has_no_merge_keys():
    # HA's YAML loader warns "duplicate key" on every load for `<<: *anchor` overrides.
    assert "<<:" not in dashboard_text()
