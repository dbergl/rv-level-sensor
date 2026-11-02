"""Adding the AC bridge: discovery or picker, then pairing."""

from unittest.mock import patch

import pytest
from homeassistant import config_entries, data_entry_flow
from homeassistant.components.bluetooth import BluetoothServiceInfo
from homeassistant.components.lovelace.const import LOVELACE_DATA
from homeassistant.config_entries import ConfigEntryState
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.rv_level import ac_client, config_flow
from custom_components.rv_level.ac_protocol import SERVICE_UUID
from custom_components.rv_level.ac_runtime import AcRuntime
from custom_components.rv_level.const import DOMAIN

from conftest import _setup
from test_ac_runtime import ADDRESS, KEY, FakeEsp, settle

OTHER = "02:00:00:00:AC:02"


def _info(address=ADDRESS, name="RV Level AC", uuids=(SERVICE_UUID,)):
    return BluetoothServiceInfo(
        name=name,
        address=address,
        rssi=-60,
        manufacturer_data={},
        service_data={},
        service_uuids=list(uuids),
        source="local",
    )


@pytest.fixture
def esp():
    """The fake ESP32 from the AcClient tests, reachable over BLE."""
    esp = FakeEsp()
    with (
        patch.object(ac_client, "_ble_device", lambda hass, address: object()),
        patch.object(ac_client, "establish_connection", esp.establish),
    ):
        yield esp


@pytest.fixture
def discovered():
    """The connectable devices HA's bluetooth integration has seen."""
    infos = []
    calls = []

    def fake(hass, connectable=True):
        calls.append(connectable)
        return infos

    with patch.object(config_flow, "async_discovered_service_info", fake):
        yield infos
    assert all(calls)


async def _start_ac(hass):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    return await hass.config_entries.flow.async_configure(
        result["flow_id"], {"next_step_id": "ac"}
    )


async def _pick(hass, address=ADDRESS):
    result = await _start_ac(hass)
    assert result["step_id"] == "ac"
    return await hass.config_entries.flow.async_configure(
        result["flow_id"], {"address": address}
    )


async def test_ac_pair_creates_entry(hass, esp, discovered, caplog):
    esp.key = None
    discovered.extend([_info(), _info(OTHER, "Phone", uuids=()), _info(OTHER.replace("02", "04"))])

    result = await _start_ac(hass)
    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["step_id"] == "ac"
    options = result["data_schema"].schema["address"].config["options"]
    assert [o["value"] for o in options] == [ADDRESS, "04:00:00:00:AC:04"]

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"address": ADDRESS}
    )
    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["step_id"] == "pair"

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    entry = result["result"]
    assert entry.unique_id == f"ac_{ADDRESS}"
    assert entry.data == {"kind": "ac", "address": ADDRESS, "key": esp.key.hex()}
    assert esp.key.hex() not in caplog.text
    await settle(hass)


async def test_ac_pair_refused_shows_error(hass, esp, discovered):
    discovered.append(_info())
    esp.pairing_open = False

    result = await _pick(hass)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["step_id"] == "pair"
    assert result["errors"] == {"base": "pair_refused"}

    # The user opens the window and tries again.
    esp.pairing_open = True
    esp.key = None
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    assert result["result"].data["key"] == esp.key.hex()
    await settle(hass)


async def test_ac_unreachable_shows_cannot_connect(hass, esp, discovered):
    discovered.append(_info())

    result = await _pick(hass)
    with patch.object(ac_client, "_ble_device", lambda hass, address: None):
        result = await hass.config_entries.flow.async_configure(result["flow_id"], {})

    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["errors"] == {"base": "cannot_connect"}


async def test_ac_no_devices_aborts(hass, discovered):
    discovered.append(_info(uuids=()))

    result = await _start_ac(hass)

    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert result["reason"] == "no_devices_found"


async def test_ac_picker_leaves_out_configured(hass, discovered):
    MockConfigEntry(
        domain=DOMAIN, unique_id=f"ac_{ADDRESS}", version=2, data={"kind": "ac", "address": ADDRESS}
    ).add_to_hass(hass)
    discovered.append(_info())

    result = await _start_ac(hass)

    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert result["reason"] == "no_devices_found"


async def test_ac_bluetooth_discovery_pairs(hass, esp):
    esp.key = None
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_BLUETOOTH}, data=_info()
    )
    assert result["type"] is data_entry_flow.FlowResultType.FORM
    assert result["step_id"] == "pair"

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    assert result["result"].unique_id == f"ac_{ADDRESS}"
    assert result["result"].data["address"] == ADDRESS
    await settle(hass)


async def test_ac_discovery_of_configured_address_aborts(hass):
    MockConfigEntry(
        domain=DOMAIN, unique_id=f"ac_{ADDRESS}", version=2, data={"kind": "ac", "address": ADDRESS}
    ).add_to_hass(hass)

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_BLUETOOTH}, data=_info()
    )

    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert result["reason"] == "already_configured"


async def test_ac_only_without_leveling_device(hass, esp, discovered):
    """No BTHome device at all: the AC is still added and set up, with no dashboard."""
    discovered.append(_info())
    esp.key = None

    result = await _pick(hass)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    entry = result["result"]
    await settle(hass)

    assert entry.state is ConfigEntryState.LOADED
    runtime = entry.runtime_data
    assert isinstance(runtime, AcRuntime)
    assert runtime.available
    assert "rv-level" not in hass.data[LOVELACE_DATA].dashboards
    # Only the AC entities, fed end to end from the fake ESP32's status.
    entities = er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)
    assert len(entities) == 17
    assert all(".countrymodpro" in e.entity_id for e in entities)
    climate = hass.states.get("climate.countrymodpro")
    assert climate.state == "cool"
    assert hass.states.get("sensor.countrymodpro_battery_voltage").state == "13"

    assert await hass.config_entries.async_unload(entry.entry_id)
    await settle(hass)
    assert entry.state is ConfigEntryState.NOT_LOADED
    assert not runtime.available
    assert not esp.link.connected


async def test_ac_entry_alongside_leveling(hass, esp, bthome_device):
    """The AC entry neither adds nor removes the leveling dashboard."""
    leveling = await _setup(hass, bthome_device)
    ac = MockConfigEntry(
        domain=DOMAIN,
        unique_id=f"ac_{ADDRESS}",
        version=2,
        data={"kind": "ac", "address": ADDRESS, "key": KEY.hex()},
    )
    ac.add_to_hass(hass)
    assert await hass.config_entries.async_setup(ac.entry_id)
    await settle(hass)
    assert ac.runtime_data.available

    assert await hass.config_entries.async_unload(ac.entry_id)
    await settle(hass)

    assert leveling.state is ConfigEntryState.LOADED
    assert "rv-level" in hass.data[LOVELACE_DATA].dashboards
    assert hass.states.get("sensor.rv_level_lift_fl") is not None
