"""Renders the package's HA templates with plain jinja2 and stand-ins for HA's template API."""
import math
import re
from pathlib import Path

import jinja2
import pytest
import yaml

HA = Path(__file__).resolve().parents[1]

DEFAULTS = {
    "input_number.rv_level_wheelbase": "159.0",
    "input_number.rv_level_track": "68.5",
    "input_number.rv_level_block_height": "1.0",
    "input_number.rv_level_tolerance": "0.5",
    "input_number.rv_level_blocks_carried": "10.0",
}


def package():
    return yaml.safe_load((HA / "packages" / "rv_level.yaml").read_text())


def entity(domain, name):
    """The template entity config with this `name` from the package."""
    for block in package()["template"]:
        for e in block.get(domain, []):
            if e["name"] == name:
                return e
    raise KeyError(name)


class Ha:
    def __init__(self, bthome=None, **overrides):
        """bthome: entity IDs the BTHome integration owns; default: every rv_level_ble_* and
        rv_level_<hex4>_* entity given a state."""
        self.states = dict(DEFAULTS)
        self.states.update({k.replace("__", "."): v for k, v in overrides.items()})
        if bthome is None:
            bthome = [e for e in self.states if re.match(r"\w+\.rv_level_(ble|[0-9a-f]{4})_", e)]
        self.bthome = list(bthome)
        env = jinja2.Environment(loader=jinja2.FileSystemLoader(HA / "custom_templates"))
        env.globals.update(
            states=lambda eid: self.states.get(eid, "unknown"),
            is_state=lambda eid, s: self.states.get(eid) == s,
            has_value=lambda eid: self.states.get(eid, "unknown") not in ("unknown", "unavailable"),
            integration_entities=lambda name: self.bthome if name == "bthome" else [],
            tan=math.tan,
            pi=math.pi,
        )
        env.tests["match"] = lambda value, pattern: re.match(pattern, value) is not None
        self.env = env

    def render(self, template):
        return self.env.from_string(template).render().strip()


@pytest.fixture
def ha():
    return Ha
