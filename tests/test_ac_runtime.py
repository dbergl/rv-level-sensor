"""AcClient and AcRuntime against a fake ESP32 at the GATT layer (firmware behavior)."""

import asyncio
import hashlib
import struct
from datetime import timedelta
from unittest.mock import patch

import pytest
from bleak.exc import BleakError
from cryptography.hazmat.primitives.asymmetric.x25519 import (
    X25519PrivateKey,
    X25519PublicKey,
)
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from homeassistant.exceptions import HomeAssistantError
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import (
    async_fire_time_changed,
    async_fire_time_changed_exact,
)

from custom_components.rv_level import ac_client, ac_runtime
from custom_components.rv_level.ac_client import CannotConnect, PairRefused, async_pair
from custom_components.rv_level.ac_protocol import (
    AUTH_UUID,
    COMMAND_UUID,
    STATUS_UUID,
    Op,
    Result,
    command_message,
    session_proof,
)
from custom_components.rv_level.ac_runtime import AcRuntime

ADDRESS = "02:00:00:00:AC:01"
KEY = bytes(range(32))


def status_bytes(setpoint=72, air=75, flags=0x03):
    head = bytes([1, flags, 0, 0, setpoint, air, 13]) + struct.pack("<H", 120)
    return head + bytes(range(21)) + bytes(4)


class FakeLink:
    """One BLE connection to the fake ESP32."""

    def __init__(self, esp, disconnected_callback):
        self.esp = esp
        self.disconnected_callback = disconnected_callback
        self.connected = True
        self.notify = {}
        self.nonce = None
        self.session = False
        self.last_counter = 0

    async def start_notify(self, uuid, callback):
        self.notify[uuid] = callback

    def _send(self, uuid, data):
        callback = self.notify.get(uuid)
        if callback is not None and self.connected:
            asyncio.get_running_loop().call_soon(callback, None, bytearray(data))

    async def write_gatt_char(self, uuid, data, response=True):
        if not self.connected:
            raise BleakError("not connected")
        data = bytes(data)
        self.esp.writes.append((uuid, data))
        if uuid == AUTH_UUID:
            self._auth(data)
        elif uuid == COMMAND_UUID:
            self._command(data)

    def _auth(self, data):
        esp = self.esp
        if data[0] == 0x01:
            if not esp.pairing_open:
                self._send(AUTH_UUID, b"\x01")
                return
            if not esp.reply:
                return
            esp_private = X25519PrivateKey.generate()
            esp_pub = esp_private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
            ha_pub = data[1:]
            shared = esp_private.exchange(X25519PublicKey.from_public_bytes(ha_pub))
            esp.key = hashlib.blake2b(
                b"RVL-AC-PAIR-v1" + ha_pub + esp_pub, key=shared, digest_size=32
            ).digest()
            self._send(AUTH_UUID, b"\x01" + esp_pub)
        elif data == b"\x02":
            if esp.key is None:
                self._send(AUTH_UUID, b"\x02")
                return
            self.nonce = esp.next_nonce()
            self._send(AUTH_UUID, b"\x02" + self.nonce)
        elif data[0] == 0x03:
            ok = (
                esp.accept_proof
                and self.nonce is not None
                and data[1:] == session_proof(esp.key, self.nonce)
            )
            self.session = ok
            self._send(AUTH_UUID, b"\x03\x01" if ok else b"\x03\x00")
            if ok:
                self._send(STATUS_UUID, esp.status)

    def _command(self, data):
        esp = self.esp
        counter, op, value = struct.unpack_from("<IBb", data)
        valid = (
            self.session
            and counter > self.last_counter
            and data == command_message(esp.key, self.nonce, counter, Op(op), value)
        )
        if not valid:
            self._send(COMMAND_UUID, struct.pack("<IB", counter, Result.BadAuth))
            return
        self.last_counter = counter
        esp.commands.append((self.nonce, counter, Op(op), value))
        if esp.reply:
            self._send(COMMAND_UUID, struct.pack("<IB", counter, esp.result))

    def push_status(self, data):
        self._send(STATUS_UUID, data)

    def drop(self):
        """The link is lost (out of range, ESP32 reboot)."""
        if self.connected:
            self.connected = False
            if self.disconnected_callback:
                self.disconnected_callback(self)

    async def disconnect(self):
        self.drop()
        return True


class FakeEsp:
    def __init__(self, key=KEY):
        self.key = key
        self.accept_proof = True
        self.pairing_open = True
        self.reply = True
        self.result = Result.Ok
        self.status = status_bytes()
        self.fail_connects = 0
        self.links = []
        self.writes = []
        self.commands = []
        self.nonces = []

    def next_nonce(self):
        nonce = bytes([len(self.nonces) + 1]) * 16
        self.nonces.append(nonce)
        return nonce

    @property
    def link(self):
        return self.links[-1]

    async def establish(self, client_class, device, name, disconnected_callback=None, **kw):
        if self.fail_connects:
            self.fail_connects -= 1
            raise BleakError("device not found")
        link = FakeLink(self, disconnected_callback)
        self.links.append(link)
        return link

    def command_writes(self):
        return [data for uuid, data in self.writes if uuid == COMMAND_UUID]


@pytest.fixture
def esp():
    esp = FakeEsp()
    with (
        patch.object(ac_client, "_ble_device", lambda hass, address: object()),
        patch.object(ac_client, "establish_connection", esp.establish),
    ):
        yield esp


@pytest.fixture
def delays():
    """Reconnect delays the runtime schedules."""
    recorded = []
    real = ac_runtime.async_call_later

    def record(hass, delay, action):
        recorded.append(delay)
        return real(hass, delay, action)

    with patch.object(ac_runtime, "async_call_later", record):
        yield recorded


async def settle(hass):
    await hass.async_block_till_done(wait_background_tasks=True)


async def advance(hass, seconds):
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=seconds))
    await settle(hass)


@pytest.fixture
async def runtime(hass, esp):
    runtime = AcRuntime(hass, ADDRESS, KEY)
    await runtime.async_start()
    await settle(hass)
    yield runtime
    await runtime.async_stop()


async def test_session_opens_and_status_notifies_listeners(hass, runtime, esp):
    assert runtime.available
    assert runtime.status.setpoint_f == 72
    calls = []
    unsub = runtime.async_add_listener(lambda: calls.append(runtime.status))
    esp.link.push_status(status_bytes(setpoint=68))
    await settle(hass)
    assert [s.setpoint_f for s in calls] == [68]
    unsub()
    esp.link.push_status(status_bytes(setpoint=70))
    await settle(hass)
    assert len(calls) == 1


async def test_malformed_status_is_ignored(hass, runtime, esp):
    esp.link.push_status(b"\x01\x02")
    await settle(hass)
    assert runtime.available
    assert runtime.status.setpoint_f == 72


async def test_session_needs_the_proof_accepted(hass, esp, delays):
    esp.accept_proof = False
    runtime = AcRuntime(hass, ADDRESS, KEY)
    await runtime.async_start()
    await settle(hass)
    assert not runtime.available
    assert esp.link.connected is False  # gave up the link
    assert delays == [1]
    await runtime.async_stop()


async def test_wrong_key_is_refused(hass, esp):
    runtime = AcRuntime(hass, ADDRESS, bytes(32))
    await runtime.async_start()
    await settle(hass)
    assert not runtime.available
    await runtime.async_stop()


async def test_esp_without_a_key_is_not_paired(hass, esp):
    esp.key = None
    runtime = AcRuntime(hass, ADDRESS, KEY)
    await runtime.async_start()
    await settle(hass)
    assert not runtime.available
    await runtime.async_stop()


async def test_client_reports_not_paired(hass, esp):
    esp.key = None
    client = ac_client.AcClient(hass, ADDRESS, KEY, lambda s: None, lambda c: None)
    await client.connect()
    with pytest.raises(ac_client.NotPaired):
        await client.start_session()
    await client.disconnect()


async def test_client_reports_session_refused(hass, esp):
    esp.accept_proof = False
    client = ac_client.AcClient(hass, ADDRESS, KEY, lambda s: None, lambda c: None)
    await client.connect()
    with pytest.raises(ac_client.SessionRefused):
        await client.start_session()
    await client.disconnect()


async def test_disconnect_goes_unavailable_and_reconnects(hass, runtime, esp, delays):
    calls = []
    runtime.async_add_listener(lambda: calls.append(runtime.available))
    esp.link.drop()
    assert not runtime.available  # promptly, inside the callback
    await settle(hass)
    assert calls == [False]
    assert delays == [1]
    await advance(hass, 1)
    assert runtime.available
    assert calls[-1] is True  # the new session's first status may notify first
    assert len(esp.links) == 2
    assert esp.nonces[0] != esp.nonces[1]


async def test_reconnect_backoff_doubles_to_60_and_resets(hass, esp, delays):
    esp.fail_connects = 9
    runtime = AcRuntime(hass, ADDRESS, KEY)
    await runtime.async_start()
    await settle(hass)
    for delay in (1, 2, 4, 8, 16, 32, 60, 60):
        assert delays[-1] == delay
        await advance(hass, delay)
    assert delays == [1, 2, 4, 8, 16, 32, 60, 60, 60]
    await advance(hass, 60)
    assert runtime.available
    esp.link.drop()
    await settle(hass)
    assert delays[-1] == 1
    await runtime.async_stop()


async def test_no_attempt_before_the_backoff(hass, runtime, esp):
    esp.link.drop()
    await settle(hass)
    async_fire_time_changed_exact(hass, dt_util.utcnow() + timedelta(seconds=0.9))
    await settle(hass)
    assert len(esp.links) == 1
    assert not runtime.available


async def test_command_ok(hass, runtime, esp):
    await runtime.command(Op.Setpoint, 70)
    await runtime.command(Op.Power, 1)
    nonce = esp.nonces[0]
    assert esp.commands == [(nonce, 1, Op.Setpoint, 70), (nonce, 2, Op.Power, 1)]


@pytest.mark.parametrize(
    "result", [Result.Refused, Result.Unreachable, Result.NotConfirmed, Result.BadAuth]
)
async def test_command_failure_raises_result_name(hass, runtime, esp, result):
    esp.result = result
    with pytest.raises(HomeAssistantError, match=result.name):
        await runtime.command(Op.Fan, 3)


async def test_bad_auth_reconnects_with_a_new_session(hass, runtime, esp, delays):
    esp.link.session = False  # the ESP32 forgot this session
    with pytest.raises(HomeAssistantError, match="BadAuth"):
        await runtime.command(Op.Fan, 3)
    await settle(hass)
    assert not runtime.available
    assert esp.links[0].connected is False
    assert delays == [1]
    await advance(hass, 1)
    assert runtime.available
    await runtime.command(Op.Fan, 3)
    assert esp.commands == [(esp.nonces[1], 1, Op.Fan, 3)]


async def test_counter_never_repeats_across_reconnects(hass, runtime, esp):
    for value in (1, 2, 3):
        await runtime.command(Op.Fan, value)
    esp.link.drop()
    await settle(hass)
    await advance(hass, 1)
    for value in (4, 5):
        await runtime.command(Op.Fan, value)
    used = [(nonce, counter) for nonce, counter, _, _ in esp.commands]
    assert len(set(used)) == len(used) == 5
    first, second = esp.nonces
    assert first != second
    assert [c for n, c in used if n == first] == [1, 2, 3]
    assert [c for n, c in used if n == second] == [1, 2]


async def test_command_while_unavailable_raises_without_sending(hass, runtime, esp):
    esp.link.drop()
    await settle(hass)
    with pytest.raises(HomeAssistantError):
        await runtime.command(Op.Power, 1)
    assert esp.command_writes() == []


async def test_command_before_start_raises(hass, esp):
    runtime = AcRuntime(hass, ADDRESS, KEY)
    with pytest.raises(HomeAssistantError):
        await runtime.command(Op.Power, 1)
    assert esp.links == []


async def test_command_times_out(hass, runtime, esp):
    esp.reply = False
    with (
        patch.object(ac_client, "COMMAND_TIMEOUT", 0.05),
        pytest.raises(HomeAssistantError),
    ):
        await runtime.command(Op.Power, 1)


async def test_command_fails_when_the_link_drops_mid_command(hass, runtime, esp):
    esp.reply = False
    task = hass.async_create_task(runtime.command(Op.Power, 1))
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    esp.link.drop()
    with pytest.raises(HomeAssistantError):
        await task
    assert not runtime.available


async def test_stop_disconnects_and_stops_retrying(hass, runtime, esp):
    link = esp.link
    await runtime.async_stop()
    await settle(hass)
    assert not runtime.available
    assert not link.connected
    await advance(hass, 120)
    assert len(esp.links) == 1


async def test_stop_while_retrying(hass, esp):
    esp.fail_connects = 1
    runtime = AcRuntime(hass, ADDRESS, KEY)
    await runtime.async_start()
    await settle(hass)
    await runtime.async_stop()
    await advance(hass, 120)
    assert esp.links == []


async def test_pair_returns_the_shared_key(hass, esp):
    esp.key = None
    key = await async_pair(hass, ADDRESS)
    assert len(key) == 32
    assert key == esp.key
    assert not esp.link.connected


async def test_pair_refused_when_window_closed(hass, esp):
    esp.pairing_open = False
    with pytest.raises(PairRefused):
        await async_pair(hass, ADDRESS)
    assert not esp.link.connected


async def test_pair_cannot_connect_when_not_seen(hass, esp):
    with (
        patch.object(ac_client, "_ble_device", lambda hass, address: None),
        pytest.raises(CannotConnect),
    ):
        await async_pair(hass, ADDRESS)


async def test_pair_cannot_connect_when_connect_fails(hass, esp):
    esp.fail_connects = 1
    with pytest.raises(CannotConnect):
        await async_pair(hass, ADDRESS)


async def test_pair_cannot_connect_on_no_reply(hass, esp):
    esp.reply = False
    with (
        patch.object(ac_client, "PAIR_TIMEOUT", 0.05),
        pytest.raises(CannotConnect),
    ):
        await async_pair(hass, ADDRESS)
    assert not esp.link.connected


async def test_pair_and_session_errors_are_distinct():
    assert not issubclass(PairRefused, CannotConnect)
    assert not issubclass(CannotConnect, PairRefused)
    assert not issubclass(ac_client.NotPaired, CannotConnect)
    assert not issubclass(ac_client.SessionRefused, CannotConnect)
