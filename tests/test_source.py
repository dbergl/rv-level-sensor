from custom_components.rv_level.source import find_level_devices, source_entities

from conftest import DEFAULT_ENTITY_IDS


async def test_finds_device_by_unique_id(hass, bthome_device):
    device_id = bthome_device()

    assert [d.id for d in find_level_devices(hass)] == [device_id]
    assert source_entities(hass, device_id) == DEFAULT_ENTITY_IDS


async def test_renamed_entities_still_found(hass, bthome_device):
    renamed = {
        "pitch": "sensor.rv_level_ble_pitch",
        "roll": "sensor.rv_level_ble_roll",
        "level": "binary_sensor.rv_level_ble_level",
        "moving": "binary_sensor.rv_level_ble_moving",
        "problem": "binary_sensor.rv_level_ble_problem",
    }
    device_id = bthome_device(entity_ids=renamed)

    assert source_entities(hass, device_id) == renamed


async def test_ignores_other_bthome_devices(hass, bthome_device):
    bthome_device(address="AA:BB:CC:DD:EE:FF", name="Thermometer", roles=("pitch",))

    assert find_level_devices(hass) == []
