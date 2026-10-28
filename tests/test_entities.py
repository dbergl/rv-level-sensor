import pytest
from homeassistant.const import STATE_OFF, STATE_ON, STATE_UNAVAILABLE
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.const import DOMAIN

from conftest import DEFAULT_ENTITY_IDS as SRC, _set_sources, _setup

CORNER_IDS = [
    f"sensor.rv_level_{kind}_{c}"
    for kind in ("lift", "blocks")
    for c in ("fl", "fr", "rl", "rr")
]
ALL_IDS = (
    ["sensor.rv_level_pitch", "sensor.rv_level_roll", *CORNER_IDS]
    + [
        f"binary_sensor.rv_level_{k}"
        for k in ("level", "moving", "problem", "short_on_blocks")
    ]
    + [
        f"number.rv_level_{k}"
        for k in ("wheelbase", "track", "block_height", "tolerance", "blocks_carried")
    ]
)


def _state(hass, entity_id):
    return hass.states.get(entity_id).state


async def test_entity_ids(hass, bthome_device):
    await _setup(hass, bthome_device)

    for entity_id in ALL_IDS:
        assert hass.states.get(entity_id) is not None, entity_id


async def test_values_follow_source(hass, bthome_device):
    await _setup(hass, bthome_device, pitch="1.0", roll="-1.0")

    assert _state(hass, "sensor.rv_level_lift_rl") == "3.97"
    assert _state(hass, "sensor.rv_level_blocks_rl") == "4"
    assert _state(hass, "binary_sensor.rv_level_short_on_blocks") == STATE_OFF
    assert _state(hass, "binary_sensor.rv_level_level") == STATE_ON
    assert _state(hass, "binary_sensor.rv_level_moving") == STATE_OFF
    assert _state(hass, "binary_sensor.rv_level_problem") == STATE_OFF


async def test_angles_rounded_before_maths(hass, bthome_device):
    await _setup(hass, bthome_device, pitch="-0.08", roll="0")

    assert _state(hass, "sensor.rv_level_pitch") == "-0.1"
    assert _state(hass, "sensor.rv_level_lift_fl") == "0.28"


async def test_sensor_offline_at_startup(hass, bthome_device):
    await _setup(
        hass,
        bthome_device,
        pitch=STATE_UNAVAILABLE,
        roll=STATE_UNAVAILABLE,
        level=STATE_UNAVAILABLE,
        moving=STATE_UNAVAILABLE,
        problem=STATE_UNAVAILABLE,
    )

    assert _state(hass, "binary_sensor.rv_level_problem") == STATE_ON
    assert _state(hass, "sensor.rv_level_lift_fl") == STATE_UNAVAILABLE
    assert _state(hass, "sensor.rv_level_blocks_fl") == STATE_UNAVAILABLE
    assert _state(hass, "binary_sensor.rv_level_short_on_blocks") == STATE_UNAVAILABLE

    _set_sources(hass, pitch="1.0", roll="-1.0")
    await hass.async_block_till_done()

    assert _state(hass, "binary_sensor.rv_level_problem") == STATE_OFF
    assert _state(hass, "sensor.rv_level_lift_rl") == "3.97"
    assert _state(hass, "sensor.rv_level_blocks_rl") == "4"


async def test_missing_problem_entity_sets_problem(hass, bthome_device):
    await _setup(
        hass, bthome_device, roles=("pitch", "roll", "level", "moving"), pitch="1"
    )

    assert _state(hass, "binary_sensor.rv_level_problem") == STATE_ON


async def test_number_change_recomputes(hass, bthome_device):
    entry = await _setup(hass, bthome_device, pitch="-1", roll="0")
    runtime = entry.runtime_data

    await hass.services.async_call(
        "number",
        "set_value",
        {"entity_id": "number.rv_level_wheelbase", "value": 114.58},
        blocking=True,
    )
    await hass.services.async_call(
        "number",
        "set_value",
        {"entity_id": "number.rv_level_tolerance", "value": 0},
        blocking=True,
    )
    await hass.async_block_till_done()

    assert entry.options["wheelbase"] == 114.58
    assert entry.options["tolerance"] == 0
    assert _state(hass, "sensor.rv_level_blocks_fl") == "2"
    assert _state(hass, "number.rv_level_wheelbase") == "114.58"
    assert entry.runtime_data is runtime


async def test_options_change_recomputes(hass, bthome_device):
    entry = await _setup(hass, bthome_device, pitch="0", roll="2")
    assert _state(hass, "sensor.rv_level_blocks_fl") == "0"

    result = await hass.config_entries.options.async_init(entry.entry_id)
    result = await hass.config_entries.options.async_configure(
        result["flow_id"],
        {"wheelbase": 159, "track": 68.5, "driver_side": "right"},
    )
    await hass.async_block_till_done()

    assert _state(hass, "sensor.rv_level_blocks_fl") == "2"
    assert _state(hass, "sensor.rv_level_blocks_fr") == "0"


async def test_metric_units(hass, bthome_device):
    await _setup(hass, bthome_device, unit="cm", pitch="1", roll="0")

    lift = hass.states.get("sensor.rv_level_lift_fl")
    assert lift.attributes["unit_of_measurement"] == "cm"
    wheelbase = hass.states.get("number.rv_level_wheelbase")
    assert wheelbase.attributes["unit_of_measurement"] == "cm"
    assert float(wheelbase.state) == 404


async def test_unload(hass, bthome_device):
    entry = await _setup(hass, bthome_device, pitch="1", roll="0")
    runtime = entry.runtime_data
    calls = []
    runtime.async_add_listener(lambda: calls.append(1))

    assert await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()

    assert _state(hass, "sensor.rv_level_lift_fl") == STATE_UNAVAILABLE
    hass.states.async_set(SRC["pitch"], "5")
    await hass.async_block_till_done()
    assert calls == []
