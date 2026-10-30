"""Live level state, computed from the BTHome sensor and the vehicle settings."""

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import STATE_ON, STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

from .const import (
    CONF_BLOCK_HEIGHT,
    CONF_BLOCKS_CARRIED,
    CONF_DEVICE_ID,
    CONF_DRIVER_SIDE,
    CONF_TOLERANCE,
    CONF_TRACK,
    CONF_WHEELBASE,
)
from .leveling import block_layers, corner_lifts, short_on_blocks
from .source import source_entities

type RvLevelConfigEntry = ConfigEntry[RvLevelRuntime]

NO_VALUE = (STATE_UNKNOWN, STATE_UNAVAILABLE)


@dataclass(frozen=True)
class Snapshot:
    pitch: float | None = None
    roll: float | None = None
    lifts: dict[str, float] | None = None
    layers: dict[str, int] | None = None
    level: bool | None = None
    moving: bool | None = None
    problem: bool = True
    short: bool | None = None


class RvLevelRuntime:
    def __init__(self, hass: HomeAssistant, entry: RvLevelConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.snapshot = Snapshot()
        self._listeners: list[Callable[[], None]] = []
        self._sources: dict[str, str | None] = {}
        self._unsub: CALLBACK_TYPE | None = None

    async def async_start(self) -> None:
        self._resubscribe()
        self._compute()

    async def async_stop(self) -> None:
        if self._unsub:
            self._unsub()
            self._unsub = None

    @callback
    def async_add_listener(self, listener: Callable[[], None]) -> Callable[[], None]:
        self._listeners.append(listener)
        return lambda: self._listeners.remove(listener)

    async def async_recompute(self) -> None:
        """Re-read the source entities and options, then notify."""
        self._resubscribe()
        self._compute()

    def _resubscribe(self) -> None:
        sources = source_entities(self.hass, self.entry.data[CONF_DEVICE_ID])
        if self._unsub and sources == self._sources:
            return
        if self._unsub:
            self._unsub()
        self._sources = sources
        self._unsub = async_track_state_change_event(
            self.hass,
            [entity_id for entity_id in sources.values() if entity_id],
            self._source_changed,
        )

    @callback
    def _source_changed(self, event) -> None:
        self._compute()

    def _state(self, role: str) -> str | None:
        """The source state, or None when it is missing or has no value."""
        entity_id = self._sources.get(role)
        state = self.hass.states.get(entity_id) if entity_id else None
        if state is None or state.state in NO_VALUE:
            return None
        return state.state

    def _angle(self, role: str) -> float | None:
        value = self._state(role)
        try:
            return None if value is None else round(float(value), 1)
        except ValueError:
            return None

    def _flag(self, role: str) -> bool | None:
        value = self._state(role)
        return None if value is None else value == STATE_ON

    def _compute(self) -> None:
        options = self.entry.options
        pitch, roll = self._angle("pitch"), self._angle("roll")
        lifts = layers = short = None
        if pitch is not None and roll is not None:
            lifts = corner_lifts(
                pitch,
                roll,
                options[CONF_WHEELBASE],
                options[CONF_TRACK],
                options[CONF_DRIVER_SIDE],
            )
            layers = {
                corner: block_layers(
                    lift, options[CONF_BLOCK_HEIGHT], options[CONF_TOLERANCE]
                )
                for corner, lift in lifts.items()
            }
            short = short_on_blocks(layers, options[CONF_BLOCKS_CARRIED])
        problem = self._flag("problem")
        self.snapshot = Snapshot(
            pitch=pitch,
            roll=roll,
            lifts=lifts,
            layers=layers,
            level=self._flag("level"),
            moving=self._flag("moving"),
            problem=problem is None or problem or pitch is None or roll is None,
            short=short,
        )
        for listener in list(self._listeners):
            listener()
