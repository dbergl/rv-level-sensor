"""Live AC state: a persistent session to the AC bridge, reconnected on loss."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable

from bleak.exc import BleakError
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_call_later

from .ac_client import AcClient
from .ac_protocol import AcStatus, Op, Result

_LOGGER = logging.getLogger(__name__)

BACKOFF_MIN = 1
BACKOFF_MAX = 60

type ClientFactory = Callable[..., AcClient]
type AcConfigEntry = ConfigEntry[AcRuntime]


class AcRuntime:
    def __init__(
        self,
        hass: HomeAssistant,
        address: str,
        key: bytes,
        client_factory: ClientFactory = AcClient,
    ) -> None:
        self.hass = hass
        self.status: AcStatus | None = None
        self._address = address
        self._key = key
        self._factory = client_factory
        self._listeners: list[Callable[[], None]] = []
        self._client: AcClient | None = None
        self._open = False
        self._backoff = BACKOFF_MIN
        self._cancel_retry: CALLBACK_TYPE | None = None
        self._task: asyncio.Task | None = None
        self._stopping = False
        self._warned = False

    @property
    def available(self) -> bool:
        """The link to the ESP32 is up and its session is open."""
        return self._open

    async def async_start(self) -> None:
        self._stopping = False
        self._start_connect()

    async def async_stop(self) -> None:
        self._stopping = True
        if self._cancel_retry:
            self._cancel_retry()
            self._cancel_retry = None
        if self._task and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        self._task = None
        client, self._client = self._client, None
        self._set_open(False)
        if client is not None:
            await client.disconnect()

    @callback
    def async_add_listener(self, listener: Callable[[], None]) -> Callable[[], None]:
        self._listeners.append(listener)
        return lambda: self._listeners.remove(listener)

    async def command(self, op: Op, value: int) -> None:
        """Send one command; raises unless the AC bridge answers Ok. Never queued."""
        client = self._client
        if not self._open or client is None:
            raise HomeAssistantError("The AC bridge is not connected")
        try:
            result = await client.send(op, value)
        except (HomeAssistantError, BleakError) as err:
            raise HomeAssistantError(f"AC {op.name} failed: {err}") from err
        if result is Result.BadAuth:
            # The ESP32 no longer knows this session; start a fresh one.
            self._lost(client)
        if result is not Result.Ok:
            raise HomeAssistantError(f"AC {op.name} failed: {result.name}")

    def _notify(self) -> None:
        for listener in list(self._listeners):
            listener()

    def _set_open(self, is_open: bool) -> None:
        if self._open != is_open:
            self._open = is_open
            self._notify()

    def _start_connect(self) -> None:
        self._task = self.hass.async_create_background_task(
            self._async_connect(), f"rv_level AC bridge {self._address}"
        )

    async def _async_connect(self) -> None:
        client = self._factory(
            self.hass, self._address, self._key, self._on_status, self._lost
        )
        self._client = client
        try:
            await client.connect()
            await client.start_session()
        except Exception as err:  # noqa: BLE001 - any failure must lead to a retry
            log = _LOGGER.debug if self._warned else _LOGGER.warning
            log("AC bridge %s: %s", self._address, err)
            self._warned = True
            if self._client is client:
                self._client = None
                await client.disconnect()
                self._schedule_retry()
            return
        if self._client is not client:
            return  # lost while opening; _lost already scheduled the retry
        if self._warned:
            _LOGGER.info("AC bridge %s connected", self._address)
        self._warned = False
        self._backoff = BACKOFF_MIN
        self._set_open(True)

    @callback
    def _on_status(self, status: AcStatus) -> None:
        self.status = status
        self._notify()

    @callback
    def _lost(self, client: AcClient) -> None:
        """The link or the session is gone: unavailable now, reconnect later."""
        if client is not self._client:
            return
        self._client = None
        if self._open:
            _LOGGER.info("AC bridge %s disconnected", self._address)
        self._set_open(False)
        self.hass.async_create_background_task(
            client.disconnect(), "rv_level AC bridge disconnect"
        )
        self._schedule_retry()

    def _schedule_retry(self) -> None:
        if self._stopping:
            return
        if self._cancel_retry:
            self._cancel_retry()
        delay = self._backoff
        self._backoff = min(self._backoff * 2, BACKOFF_MAX)
        self._cancel_retry = async_call_later(self.hass, delay, self._retry)

    @callback
    def _retry(self, _now) -> None:
        self._cancel_retry = None
        if not self._stopping:
            self._start_connect()
