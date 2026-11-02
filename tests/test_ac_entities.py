"""The CountryModPro entities on the AC entry, against a fake AcRuntime."""

from dataclasses import replace
from unittest.mock import patch

import pytest
from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.util.unit_system import US_CUSTOMARY_SYSTEM
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components import rv_level
from custom_components.rv_level.ac_protocol import AcStatus, Mode, Op
from custom_components.rv_level.const import DOMAIN

ADDRESS = "02:00:00:00:AC:01"

ENTITY_IDS = {
    "climate.countrymodpro",
    "switch.countrymodpro_power",
    "binary_sensor.countrymodpro_power",
    "select.countrymodpro_operating_mode",
    "sensor.countrymodpro_mode",
    "select.countrymodpro_fan_level",
    "number.countrymodpro_setpoint",
    "sensor.countrymodpro_air_temperature",
    "sensor.countrymodpro_battery_voltage",
    "sensor.countrymodpro_low_voltage_protection",
    "sensor.countrymodpro_status_frame",
    "switch.countrymodpro_display_in_celsius",
    "switch.countrymodpro_light",
    "switch.countrymodpro_display",
    "switch.countrymodpro_vent_oscillation",
    "binary_sensor.countrymodpro_ac_responding",
    "binary_sensor.countrymodpro_unit_in_celsius",
}
RESPONDING = "binary_sensor.countrymodpro_ac_responding"
CLIMATE = "climate.countrymodpro"

STATUS = AcStatus(
    responding=True,
    power=True,
    display=True,
    light=False,
    oscillating=True,
    celsius=False,
    mode=Mode.Eco,
    fan=3,
    setpoint_f=72,
    air_temp_f=75,
    battery_v=13,
    low_voltage_protection=11.5,
    raw=bytes([0xA5, 0x0F]) + bytes(19),
)


class FakeRuntime:
    """AcRuntime's surface as the entities see it."""

    def __init__(self, hass, address, key):
        self.status = STATUS
        self.available = True
        self.commands = []
        self.error = None
        self._listeners = []

    async def async_start(self):
        pass

    async def async_stop(self):
        pass

    def async_add_listener(self, listener):
        self._listeners.append(listener)
        return lambda: self._listeners.remove(listener)

    async def command(self, op, value):
        self.commands.append((op, value))
        if self.error:
            raise HomeAssistantError(self.error)

    def push(self, status=None, available=None):
        if status is not None:
            self.status = status
        if available is not None:
            self.available = available
        for listener in list(self._listeners):
            listener()


@pytest.fixture
async def runtime(hass):
    hass.config.units = US_CUSTOMARY_SYSTEM
    entry = MockConfigEntry(
        domain=DOMAIN,
        unique_id=f"ac_{ADDRESS}",
        version=2,
        data={"kind": "ac", "address": ADDRESS, "key": bytes(32).hex()},
    )
    entry.add_to_hass(hass)
    with patch.object(rv_level, "AcRuntime", FakeRuntime):
        assert await hass.config_entries.async_setup(entry.entry_id)
        await hass.async_block_till_done()
    runtime = entry.runtime_data
    runtime.entry = entry
    return runtime


def _state(hass, entity_id):
    return hass.states.get(entity_id).state


async def _push(hass, runtime, **kw):
    runtime.push(**kw)
    await hass.async_block_till_done()


async def test_entity_ids_match_countrymodpro(hass, runtime):
    entries = er.async_entries_for_config_entry(er.async_get(hass), runtime.entry.entry_id)
    assert {e.entity_id for e in entries} == ENTITY_IDS
    by_id = {e.entity_id: e for e in entries}
    for entity_id in (
        "sensor.countrymodpro_low_voltage_protection",
        "sensor.countrymodpro_status_frame",
    ):
        assert by_id[entity_id].entity_category is er.EntityCategory.DIAGNOSTIC

    device = dr.async_get(hass).async_get(entries[0].device_id)
    assert device.name == "CountryModPro"
    assert device.manufacturer == "CountryModPro"
    assert device.model == "Rooftop AC"
    assert device.via_device_id is None
    assert {e.device_id for e in entries} == {device.id}


async def test_climate_state_from_status(hass, runtime):
    state = hass.states.get(CLIMATE)
    assert state.state == "cool"
    assert state.attributes["hvac_modes"] == ["off", "cool", "fan_only"]
    assert state.attributes["preset_modes"] == ["none", "eco", "sleep", "boost"]
    assert state.attributes["fan_modes"] == ["auto", "1", "2", "3", "4", "5"]
    assert state.attributes["min_temp"] == 61
    assert state.attributes["max_temp"] == 86
    assert state.attributes["target_temp_step"] == 1
    assert state.attributes["preset_mode"] == "eco"
    assert state.attributes["fan_mode"] == "3"
    assert state.attributes["temperature"] == 72
    assert state.attributes["current_temperature"] == 75
    assert "hvac_action" not in state.attributes

    await _push(hass, runtime, status=replace(STATUS, mode=Mode.Turbo, fan=0))
    state = hass.states.get(CLIMATE)
    assert state.state == "cool"
    assert state.attributes["preset_mode"] == "boost"
    assert state.attributes["fan_mode"] == "auto"

    await _push(hass, runtime, status=replace(STATUS, mode=Mode.Cool))
    assert hass.states.get(CLIMATE).attributes["preset_mode"] == "none"

    await _push(hass, runtime, status=replace(STATUS, mode=Mode.FanOnly))
    assert _state(hass, CLIMATE) == "fan_only"

    await _push(hass, runtime, status=replace(STATUS, power=False))
    assert _state(hass, CLIMATE) == "off"


async def test_off_hides_mode_and_fan(hass, runtime):
    """The mode and fan reported while off aren't trusted; the panel shows neither."""
    await _push(hass, runtime, status=replace(STATUS, power=False))
    state = hass.states.get(CLIMATE)
    assert state.state == "off"
    assert state.attributes["preset_mode"] is None
    assert state.attributes["fan_mode"] is None
    for entity_id in (
        "select.countrymodpro_operating_mode",
        "sensor.countrymodpro_mode",
        "select.countrymodpro_fan_level",
    ):
        assert _state(hass, entity_id) == STATE_UNKNOWN, entity_id
    # The rest is still the unit's report.
    assert state.attributes["temperature"] == 72
    assert state.attributes["current_temperature"] == 75
    assert _state(hass, "number.countrymodpro_setpoint") == "72"
    assert _state(hass, "sensor.countrymodpro_air_temperature") == "75"
    assert _state(hass, "sensor.countrymodpro_battery_voltage") == "13"
    assert _state(hass, "switch.countrymodpro_power") == "off"
    assert _state(hass, "switch.countrymodpro_display") == "on"


@pytest.mark.parametrize("mode", [Mode.Dehumid, Mode.Unknown])
async def test_unreported_mode_is_unknown(hass, runtime, mode):
    """Nothing is guessed for a mode HA doesn't offer."""
    await _push(hass, runtime, status=replace(STATUS, mode=mode))
    state = hass.states.get(CLIMATE)
    assert state.state == STATE_UNKNOWN
    assert state.attributes["preset_mode"] is None
    assert _state(hass, "select.countrymodpro_operating_mode") == STATE_UNKNOWN
    assert _state(hass, "sensor.countrymodpro_mode") == STATE_UNKNOWN
    # The rest of the status is still the unit's own report.
    assert _state(hass, "switch.countrymodpro_power") == "on"
    assert state.attributes["temperature"] == 72


async def test_unreported_fan_is_unknown(hass, runtime):
    await _push(hass, runtime, status=replace(STATUS, fan=9))
    assert _state(hass, "select.countrymodpro_fan_level") == STATE_UNKNOWN
    assert hass.states.get(CLIMATE).attributes["fan_mode"] is None


async def test_other_entities_from_status(hass, runtime):
    expected = {
        "switch.countrymodpro_power": "on",
        "binary_sensor.countrymodpro_power": "on",
        "select.countrymodpro_operating_mode": "eco",
        "sensor.countrymodpro_mode": "eco",
        "select.countrymodpro_fan_level": "3",
        "number.countrymodpro_setpoint": "72",
        "sensor.countrymodpro_air_temperature": "75",
        "sensor.countrymodpro_battery_voltage": "13",
        "sensor.countrymodpro_low_voltage_protection": "11.5",
        "sensor.countrymodpro_status_frame": "a5 0f" + " 00" * 19,
        "switch.countrymodpro_display_in_celsius": "off",
        "switch.countrymodpro_light": "off",
        "switch.countrymodpro_display": "on",
        "switch.countrymodpro_vent_oscillation": "on",
        RESPONDING: "on",
        "binary_sensor.countrymodpro_unit_in_celsius": "off",
    }
    assert {e: _state(hass, e) for e in expected} == expected

    assert hass.states.get("select.countrymodpro_operating_mode").attributes["options"] == [
        "cool", "eco", "sleep", "turbo", "fan_only"
    ]
    assert hass.states.get("select.countrymodpro_fan_level").attributes["options"] == [
        "auto", "1", "2", "3", "4", "5"
    ]
    setpoint = hass.states.get("number.countrymodpro_setpoint").attributes
    assert (setpoint["min"], setpoint["max"], setpoint["step"]) == (61, 86, 1)
    assert setpoint["unit_of_measurement"] == "°F"
    assert (
        hass.states.get("sensor.countrymodpro_air_temperature").attributes["unit_of_measurement"]
        == "°F"
    )
    for entity_id in ("battery_voltage", "low_voltage_protection"):
        attrs = hass.states.get(f"sensor.countrymodpro_{entity_id}").attributes
        assert attrs["unit_of_measurement"] == "V"
    assert hass.states.get(RESPONDING).attributes["device_class"] == "connectivity"
    assert (
        hass.states.get("binary_sensor.countrymodpro_unit_in_celsius").attributes["device_class"]
        == "problem"
    )

    await _push(hass, runtime, status=replace(STATUS, power=False, light=True, display=False))
    assert _state(hass, "switch.countrymodpro_power") == "off"
    assert _state(hass, "binary_sensor.countrymodpro_power") == "off"
    assert _state(hass, "switch.countrymodpro_light") == "on"
    assert _state(hass, "switch.countrymodpro_display") == "off"


COOL = replace(STATUS, mode=Mode.Cool)
OFF_IN_ECO = replace(STATUS, power=False, mode=Mode.Eco)
FAN_ONLY = replace(STATUS, mode=Mode.FanOnly)


@pytest.mark.parametrize(
    ("status", "domain", "service", "entity_id", "data", "expected"),
    [
        (STATUS, "climate", "set_hvac_mode", CLIMATE, {"hvac_mode": "off"}, (Op.Power, 0)),
        (STATUS, "climate", "set_hvac_mode", CLIMATE, {"hvac_mode": "cool"}, (Op.Mode, Mode.Cool)),
        (STATUS, "climate", "set_hvac_mode", CLIMATE, {"hvac_mode": "fan_only"},
         (Op.Mode, Mode.FanOnly)),
        (STATUS, "climate", "turn_off", CLIMATE, {}, (Op.Power, 0)),
        (STATUS, "climate", "turn_on", CLIMATE, {}, (Op.Power, 1)),
        # Preset none only clears a preset that is on; anything else sends nothing.
        (COOL, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "none"}, None),
        (FAN_ONLY, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "none"}, None),
        (OFF_IN_ECO, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "none"}, None),
        (STATUS, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "none"},
         (Op.ClearPreset, 0)),
        (COOL, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "eco"}, (Op.Mode, Mode.Eco)),
        (COOL, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "sleep"},
         (Op.Mode, Mode.Sleep)),
        (COOL, "climate", "set_preset_mode", CLIMATE, {"preset_mode": "boost"},
         (Op.Mode, Mode.Turbo)),
        (STATUS, "climate", "set_fan_mode", CLIMATE, {"fan_mode": "auto"}, (Op.Fan, 0)),
        (STATUS, "climate", "set_fan_mode", CLIMATE, {"fan_mode": "5"}, (Op.Fan, 5)),
        (STATUS, "climate", "set_temperature", CLIMATE, {"temperature": 68}, (Op.Setpoint, 68)),
        (STATUS, "number", "set_value", "number.countrymodpro_setpoint", {"value": 80},
         (Op.Setpoint, 80)),
        (STATUS, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "cool"}, (Op.ClearPreset, 0)),
        (COOL, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "cool"}, (Op.Mode, Mode.Cool)),
        # Off: the reported mode isn't a preset to clear; Mode Cool powers the unit on.
        (OFF_IN_ECO, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "cool"}, (Op.Mode, Mode.Cool)),
        (COOL, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "eco"}, (Op.Mode, Mode.Eco)),
        (COOL, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "sleep"}, (Op.Mode, Mode.Sleep)),
        (COOL, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "turbo"}, (Op.Mode, Mode.Turbo)),
        (COOL, "select", "select_option", "select.countrymodpro_operating_mode",
         {"option": "fan_only"}, (Op.Mode, Mode.FanOnly)),
        (STATUS, "select", "select_option", "select.countrymodpro_fan_level",
         {"option": "auto"}, (Op.Fan, 0)),
        (STATUS, "select", "select_option", "select.countrymodpro_fan_level",
         {"option": "2"}, (Op.Fan, 2)),
        (STATUS, "switch", "turn_off", "switch.countrymodpro_power", {}, (Op.Power, 0)),
        (OFF_IN_ECO, "switch", "turn_on", "switch.countrymodpro_power", {}, (Op.Power, 1)),
        (STATUS, "switch", "turn_on", "switch.countrymodpro_display_in_celsius", {},
         (Op.Celsius, 1)),
        (STATUS, "switch", "turn_on", "switch.countrymodpro_light", {}, (Op.Light, 1)),
        (STATUS, "switch", "turn_off", "switch.countrymodpro_display", {}, (Op.Display, 0)),
        (STATUS, "switch", "turn_off", "switch.countrymodpro_vent_oscillation", {},
         (Op.Oscillate, 0)),
    ],
)
async def test_climate_commands_map_to_ops(
    hass, runtime, status, domain, service, entity_id, data, expected
):
    await _push(hass, runtime, status=status)
    before = hass.states.get(entity_id)

    await hass.services.async_call(
        domain, service, {"entity_id": entity_id, **data}, blocking=True
    )
    await hass.async_block_till_done()

    assert runtime.commands == ([expected] if expected else [])
    assert all(type(v) is int for _, v in runtime.commands)
    # Not optimistic: the state only moves with the unit's next status.
    after = hass.states.get(entity_id)
    assert (after.state, after.attributes) == (before.state, before.attributes)


async def test_command_failure_surfaces(hass, runtime):
    runtime.error = "AC Setpoint failed: Refused"
    with pytest.raises(HomeAssistantError, match="Refused"):
        await hass.services.async_call(
            "number", "set_value",
            {"entity_id": "number.countrymodpro_setpoint", "value": 70}, blocking=True,
        )


async def test_unavailable_when_link_down(hass, runtime):
    await _push(hass, runtime, available=False)
    for entity_id in ENTITY_IDS - {RESPONDING}:
        assert _state(hass, entity_id) == STATE_UNAVAILABLE, entity_id
    assert _state(hass, RESPONDING) == "off"

    await _push(hass, runtime, available=True)
    assert _state(hass, CLIMATE) == "cool"
    assert _state(hass, RESPONDING) == "on"


@pytest.mark.parametrize("status", [None, replace(STATUS, responding=False)])
async def test_unknown_when_unit_not_responding(hass, runtime, status):
    runtime.status = status
    await _push(hass, runtime)
    for entity_id in ENTITY_IDS - {RESPONDING}:
        assert _state(hass, entity_id) == STATE_UNKNOWN, entity_id
    attrs = hass.states.get(CLIMATE).attributes
    for attr in ("current_temperature", "temperature", "preset_mode", "fan_mode"):
        assert attrs[attr] is None, attr
    assert _state(hass, RESPONDING) == "off"


async def test_unit_in_celsius_problem(hass, runtime):
    await _push(hass, runtime, status=replace(STATUS, celsius=True))
    assert _state(hass, "binary_sensor.countrymodpro_unit_in_celsius") == "on"
    assert _state(hass, "switch.countrymodpro_display_in_celsius") == "on"
    # The firmware still reports the setpoint in °F.
    assert _state(hass, "number.countrymodpro_setpoint") == "72"
    assert hass.states.get(CLIMATE).attributes["temperature"] == 72

    # Setting a setpoint in °C is refused by the AC bridge; HA shows the error.
    runtime.error = "AC Setpoint failed: Refused"
    with pytest.raises(HomeAssistantError):
        await hass.services.async_call(
            "climate", "set_temperature", {"entity_id": CLIMATE, "temperature": 70},
            blocking=True,
        )
    assert runtime.commands == [(Op.Setpoint, 70)]
