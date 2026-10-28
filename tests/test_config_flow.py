from homeassistant import config_entries, data_entry_flow
from homeassistant.util.unit_system import METRIC_SYSTEM, US_CUSTOMARY_SYSTEM
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level.const import DOMAIN


async def _start(hass):
    return await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )


def _defaults(result):
    return {str(k): k.default() for k in result["data_schema"].schema}


async def test_single_device_skips_picker(hass, bthome_device):
    bthome_device()

    result = await _start(hass)

    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["step_id"] == "vehicle"


async def test_picker_with_two_devices(hass, bthome_device):
    bthome_device()
    second = bthome_device(
        address="02:00:00:00:AA:BB",
        name="RV Level AABB",
        entity_ids={
            "pitch": "sensor.rv_level_aabb_rotation",
            "roll": "sensor.rv_level_aabb_rotation_2",
            "level": "binary_sensor.rv_level_aabb_generic",
            "moving": "binary_sensor.rv_level_aabb_moving",
            "problem": "binary_sensor.rv_level_aabb_problem",
        },
    )

    result = await _start(hass)
    assert result["step_id"] == "user"
    options = result["data_schema"].schema["device_id"].config["options"]
    assert {o["label"] for o in options} == {"RV Level 3032", "RV Level AABB"}

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"device_id": second}
    )
    assert result["step_id"] == "vehicle"
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _defaults(result)
    )

    entry = result["result"]
    assert entry.title == "RV Level"
    assert entry.unique_id == "02:00:00:00:AA:BB"
    assert entry.data["device_id"] == second
    assert entry.data["address"] == "02:00:00:00:AA:BB"


async def test_no_device_aborts(hass):
    result = await _start(hass)

    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert result["reason"] == "no_devices"


async def test_defaults_follow_units_and_country(hass, bthome_device):
    bthome_device()
    hass.config.units = METRIC_SYSTEM
    hass.config.country = "GB"

    result = await _start(hass)
    assert _defaults(result) == {
        "wheelbase": 404,
        "track": 174,
        "driver_side": "right",
    }
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _defaults(result)
    )
    entry = result["result"]
    assert entry.data["unit"] == "cm"
    assert entry.options == {
        "wheelbase": 404,
        "track": 174,
        "driver_side": "right",
        "block_height": 2.5,
        "tolerance": 1.25,
        "blocks_carried": 10,
    }


async def test_us_defaults(hass, bthome_device):
    bthome_device()
    hass.config.units = US_CUSTOMARY_SYSTEM
    hass.config.country = "US"

    result = await _start(hass)
    assert _defaults(result) == {
        "wheelbase": 159,
        "track": 68.5,
        "driver_side": "left",
    }
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], _defaults(result)
    )
    assert result["result"].data["unit"] == "in"
    assert result["result"].options["block_height"] == 1.0


async def test_options_flow_updates_vehicle(hass, bthome_device):
    device_id = bthome_device()
    entry = MockConfigEntry(
        domain=DOMAIN,
        unique_id="02:00:00:00:30:32",
        data={"device_id": device_id, "address": "02:00:00:00:30:32", "unit": "in"},
        options={
            "wheelbase": 159,
            "track": 68.5,
            "driver_side": "left",
            "block_height": 1.5,
            "tolerance": 0.25,
            "blocks_carried": 7,
        },
    )
    entry.add_to_hass(hass)

    result = await hass.config_entries.options.async_init(entry.entry_id)
    assert result["step_id"] == "init"
    result = await hass.config_entries.options.async_configure(
        result["flow_id"],
        {"wheelbase": 170, "track": 72, "driver_side": "right"},
    )

    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    assert entry.options == {
        "wheelbase": 170,
        "track": 72,
        "driver_side": "right",
        "block_height": 1.5,
        "tolerance": 0.25,
        "blocks_carried": 7,
    }


async def test_single_instance(hass, bthome_device):
    bthome_device()
    MockConfigEntry(domain=DOMAIN, unique_id="other").add_to_hass(hass)

    result = await _start(hass)

    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert result["reason"] == "single_instance_allowed"
