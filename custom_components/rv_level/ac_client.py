"""Bluetooth client for the AC bridge's paired GATT service."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from typing import TYPE_CHECKING, Any

from bleak.exc import BleakError
from bleak_retry_connector import BleakClientWithServiceCache, establish_connection
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError

from .ac_protocol import (
    AUTH_UUID,
    COMMAND_UUID,
    STATUS_UUID,
    AcStatus,
    Op,
    Result,
    command_message,
    pair_key,
    pair_request,
    parse_pair_reply,
    parse_prove_reply,
    parse_session_reply,
    prove_request,
    session_request,
    unpack_result,
    unpack_status,
)

if TYPE_CHECKING:
    from bleak.backends.device import BLEDevice

_LOGGER = logging.getLogger(__name__)

AUTH_TIMEOUT = 10
COMMAND_TIMEOUT = 15
PAIR_TIMEOUT = 15


class CannotConnect(HomeAssistantError):
    """The ESP32 is not reachable, the link dropped, or it did not answer."""


class PairRefused(HomeAssistantError):
    """The ESP32's pairing window is closed."""


class NotPaired(HomeAssistantError):
    """The ESP32 holds no pairing key."""


class SessionRefused(HomeAssistantError):
    """The ESP32 rejected our proof of the pairing key (it was re-paired)."""


class CommandTimeout(HomeAssistantError):
    """No result for a command within COMMAND_TIMEOUT."""


def _ble_device(hass: HomeAssistant, address: str) -> BLEDevice | None:
    # Imported here: the bluetooth component pulls in USB tooling the test env lacks.
    from homeassistant.components import bluetooth

    return bluetooth.async_ble_device_from_address(hass, address, connectable=True)


async def _connect(
    hass: HomeAssistant, address: str, disconnected: Callable[[Any], None] | None = None
) -> Any:
    device = _ble_device(hass, address)
    if device is None:
        raise CannotConnect(f"{address} is not in range")
    try:
        return await establish_connection(
            BleakClientWithServiceCache,
            device,
            getattr(device, "name", None) or address,
            disconnected_callback=disconnected,
            ble_device_callback=lambda: _ble_device(hass, address) or device,
        )
    except (BleakError, TimeoutError) as err:
        raise CannotConnect(f"cannot connect to {address}: {err}") from err


async def async_pair(hass: HomeAssistant, address: str) -> bytes:
    """Pair with the ESP32 during its pairing window; returns the 32-byte key.

    BlueZ negotiates the ATT MTU itself on connect (it asks for 517), so the
    33-byte reply fits; a too-small MTU shows up as PairRefused.
    """
    client = await _connect(hass, address)
    reply: asyncio.Future[bytes] = asyncio.get_running_loop().create_future()

    def on_auth(_char: Any, data: bytearray) -> None:
        if not reply.done():
            reply.set_result(bytes(data))

    try:
        await client.start_notify(AUTH_UUID, on_auth)
        private = X25519PrivateKey.generate()
        pub = private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        await client.write_gatt_char(AUTH_UUID, pair_request(pub), response=True)
        async with asyncio.timeout(PAIR_TIMEOUT):
            data = await reply
        esp_pub = parse_pair_reply(data)
        if esp_pub is None:
            raise PairRefused("the AC bridge is not in pairing mode")
        return pair_key(private, esp_pub)
    except (BleakError, TimeoutError, ValueError) as err:
        raise CannotConnect(f"pairing with {address} failed: {err!r}") from err
    finally:
        try:
            await client.disconnect()
        except BleakError:
            pass


class AcClient:
    """One connection and session to the ESP32. Not reusable after a disconnect."""

    def __init__(
        self,
        hass: HomeAssistant,
        address: str,
        key: bytes,
        on_status: Callable[[AcStatus], None],
        on_disconnect: Callable[[AcClient], None],
    ) -> None:
        self._hass = hass
        self._address = address
        self._key = key
        self._on_status = on_status
        self._on_disconnect = on_disconnect
        self._client: Any = None
        self._nonce: bytes | None = None
        self._counter = 0
        self._auth_reply: asyncio.Future[bytes] | None = None
        self._pending: dict[int, asyncio.Future[Result]] = {}
        self._send_lock = asyncio.Lock()
        self._lost = False

    async def connect(self) -> None:
        self._client = await _connect(self._hass, self._address, self._disconnected)
        try:
            await self._client.start_notify(AUTH_UUID, self._on_auth)
            await self._client.start_notify(STATUS_UUID, self._on_status_notify)
            await self._client.start_notify(COMMAND_UUID, self._on_command)
        except BleakError as err:
            await self.disconnect()
            raise CannotConnect(f"cannot subscribe: {err}") from err

    async def disconnect(self) -> None:
        self._nonce = None
        client, self._client = self._client, None
        if client is not None:
            try:
                await client.disconnect()
            except BleakError:
                pass

    async def start_session(self) -> None:
        """Challenge, prove the key; the session is open only after 03 01."""
        nonce = parse_session_reply(await self._auth(session_request()))
        if nonce is None:
            raise NotPaired("the AC bridge has no pairing key")
        if not parse_prove_reply(await self._auth(prove_request(self._key, nonce))):
            raise SessionRefused("the AC bridge refused the pairing key")
        self._nonce = nonce
        self._counter = 0

    async def send(self, op: Op, value: int) -> Result:
        """One command at a time; the counter is never reused within the session."""
        async with self._send_lock:
            if self._nonce is None or self._lost:
                raise CannotConnect("no session")
            self._counter += 1
            counter = self._counter
            future: asyncio.Future[Result] = asyncio.get_running_loop().create_future()
            self._pending[counter] = future
            try:
                await self._write(
                    COMMAND_UUID, command_message(self._key, self._nonce, counter, op, value)
                )
                async with asyncio.timeout(COMMAND_TIMEOUT):
                    return await future
            except TimeoutError as err:
                raise CommandTimeout(f"no result for {op.name}") from err
            finally:
                self._pending.pop(counter, None)

    async def _auth(self, request: bytes) -> bytes:
        self._auth_reply = asyncio.get_running_loop().create_future()
        try:
            await self._write(AUTH_UUID, request)
            async with asyncio.timeout(AUTH_TIMEOUT):
                return await self._auth_reply
        except (TimeoutError, ValueError) as err:
            raise CannotConnect(f"session setup failed: {err!r}") from err
        finally:
            self._auth_reply = None

    async def _write(self, uuid: str, data: bytes) -> None:
        if self._client is None or self._lost:
            raise CannotConnect("not connected")
        try:
            await self._client.write_gatt_char(uuid, data, response=True)
        except BleakError as err:
            raise CannotConnect(f"write failed: {err}") from err

    def _on_auth(self, _char: Any, data: bytearray) -> None:
        if self._auth_reply is not None and not self._auth_reply.done():
            self._auth_reply.set_result(bytes(data))

    def _on_status_notify(self, _char: Any, data: bytearray) -> None:
        try:
            status = unpack_status(bytes(data))
        except ValueError as err:
            _LOGGER.warning("Ignoring AC status %s: %s", bytes(data).hex(), err)
            return
        self._on_status(status)

    def _on_command(self, _char: Any, data: bytearray) -> None:
        try:
            counter, result = unpack_result(bytes(data))
        except ValueError as err:
            _LOGGER.warning("Ignoring AC result %s: %s", bytes(data).hex(), err)
            return
        future = self._pending.get(counter)
        if future is not None and not future.done():
            future.set_result(result)

    def _disconnected(self, _client: Any) -> None:
        if self._lost:
            return
        self._lost = True
        self._nonce = None
        error = CannotConnect("the AC bridge disconnected")
        for future in [self._auth_reply, *self._pending.values()]:
            if future is not None and not future.done():
                future.set_exception(error)
        self._on_disconnect(self)
