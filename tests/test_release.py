"""Checks that keep the release metadata consistent."""

import json
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
COMPONENT = ROOT / "custom_components" / "rv_level"


def _version_tuple(version: str) -> tuple[int, ...]:
    return tuple(int(part) for part in version.split("."))


def test_manifest_version_matches_tag() -> None:
    result = subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        pytest.skip("no tags yet")
    manifest = json.loads((COMPONENT / "manifest.json").read_text())
    assert manifest["version"] == result.stdout.strip().removeprefix("v")


def test_hacs_json() -> None:
    hacs = json.loads((ROOT / "hacs.json").read_text())
    assert hacs["name"] == "RV Level"
    assert _version_tuple(hacs["homeassistant"]) >= (2026, 10, 0)


def test_translations_match_strings() -> None:
    strings = json.loads((COMPONENT / "strings.json").read_text())
    english = json.loads((COMPONENT / "translations" / "en.json").read_text())
    assert english == strings
