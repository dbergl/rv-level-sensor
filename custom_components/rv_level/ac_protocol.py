"""Wire format of the AC bridge's paired BLE service (firmware lib/acbridge/ha_link).

Pure functions, no Bluetooth I/O.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from enum import IntEnum

from cryptography.hazmat.primitives.asymmetric.x25519 import (
    X25519PrivateKey,
    X25519PublicKey,
)
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

SERVICE_UUID = "af480101-29e2-47f0-a341-4f1a49271bf4"
AUTH_UUID = "af480102-29e2-47f0-a341-4f1a49271bf4"
STATUS_UUID = "af480103-29e2-47f0-a341-4f1a49271bf4"
COMMAND_UUID = "af480104-29e2-47f0-a341-4f1a49271bf4"

_PAIR_LABEL = b"RVL-AC-PAIR-v1"
_CMD_LABEL = b"RVL-AC-CMD-v1"
_SESSION_LABEL = b"RVL-AC-SESSION-v1"

_AUTH_PAIR = 0x01
_AUTH_SESSION = 0x02
_AUTH_PROVE = 0x03

KEY_LEN = 32
NONCE_LEN = 16
STATUS_LEN = 34
RESULT_LEN = 5
STATUS_VERSION = 1
_UNKNOWN = 0xFF


class Op(IntEnum):
    """ac::Op"""

    Power = 0
    Mode = 1
    Fan = 2
    Setpoint = 3
    StepSetpoint = 4
    Display = 5
    Oscillate = 6
    Light = 7
    Celsius = 8
    ClearPreset = 9


class Mode(IntEnum):
    """cm::Mode"""

    Cool = 0
    Eco = 1
    Sleep = 2
    Turbo = 3
    FanOnly = 4
    Dehumid = 5
    Unknown = 6


class Result(IntEnum):
    """ac::Result"""

    Ok = 0
    Refused = 1
    Unreachable = 2
    NotConfirmed = 3
    BadAuth = 4


@dataclass(frozen=True)
class AcStatus:
    responding: bool
    power: bool
    display: bool
    light: bool
    oscillating: bool
    celsius: bool
    mode: Mode
    fan: int | None  # 0 auto, 1-5
    setpoint_f: int | None
    air_temp_f: int | None
    battery_v: int | None
    low_voltage_protection: float
    raw: bytes


def _opt(b: int) -> int | None:
    return None if b == _UNKNOWN else b


def unpack_status(data: bytes) -> AcStatus:
    """packStatus layout; ValueError on a wrong length or version."""
    if len(data) != STATUS_LEN:
        raise ValueError(f"status is {len(data)} bytes, expected {STATUS_LEN}")
    if data[0] != STATUS_VERSION:
        raise ValueError(f"unsupported status version {data[0]}")
    flags = data[1]
    return AcStatus(
        responding=bool(flags & 0x01),
        power=bool(flags & 0x02),
        display=bool(flags & 0x04),
        light=bool(flags & 0x08),
        oscillating=bool(flags & 0x10),
        celsius=bool(flags & 0x20),
        mode=Mode(data[2]),
        fan=_opt(data[3]),
        setpoint_f=_opt(data[4]),
        air_temp_f=_opt(data[5]),
        battery_v=_opt(data[6]),
        low_voltage_protection=struct.unpack_from("<H", data, 7)[0] / 10,
        raw=bytes(data[9:30]),
    )


def pair_key(private: X25519PrivateKey, esp_pub: bytes) -> bytes:
    """The 32-byte pairing key. ValueError on a low-order ESP32 key."""
    shared = private.exchange(X25519PublicKey.from_public_bytes(esp_pub))
    ha_pub = private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    return hashlib.blake2b(
        _PAIR_LABEL + ha_pub + esp_pub, key=shared, digest_size=KEY_LEN
    ).digest()


def session_proof(key: bytes, nonce: bytes) -> bytes:
    return hashlib.blake2b(_SESSION_LABEL + nonce, key=key, digest_size=16).digest()


def command_message(key: bytes, nonce: bytes, counter: int, op: Op, value: int) -> bytes:
    """counter (u32 LE) || op u8 || value i8 || mac16."""
    body = struct.pack("<Bb", op, value)
    mac = hashlib.blake2b(
        _CMD_LABEL + nonce + struct.pack("<I", counter) + body, key=key, digest_size=16
    ).digest()
    return struct.pack("<I", counter) + body + mac


def unpack_result(data: bytes) -> tuple[int, Result]:
    """counter (u32 LE) || result u8; ValueError if malformed."""
    if len(data) != RESULT_LEN:
        raise ValueError(f"result is {len(data)} bytes, expected {RESULT_LEN}")
    counter, result = struct.unpack("<IB", data)
    return counter, Result(result)


def pair_request(pub: bytes) -> bytes:
    if len(pub) != 32:
        raise ValueError("X25519 public key must be 32 bytes")
    return bytes([_AUTH_PAIR]) + pub


def parse_pair_reply(data: bytes) -> bytes | None:
    """The ESP32 public key, or None if it refused (pairing window closed)."""
    if data[:1] != bytes([_AUTH_PAIR]):
        raise ValueError(f"unexpected pairing reply {data.hex()}")
    if len(data) == 1:
        return None
    if len(data) != 33:
        raise ValueError(f"pairing reply is {len(data)} bytes")
    return bytes(data[1:])


def session_request() -> bytes:
    return bytes([_AUTH_SESSION])


def parse_session_reply(data: bytes) -> bytes | None:
    """The challenge nonce, or None if the ESP32 has no pairing key."""
    if data[:1] != bytes([_AUTH_SESSION]):
        raise ValueError(f"unexpected session reply {data.hex()}")
    if len(data) == 1:
        return None
    if len(data) != 1 + NONCE_LEN:
        raise ValueError(f"session reply is {len(data)} bytes")
    return bytes(data[1:])


def prove_request(key: bytes, nonce: bytes) -> bytes:
    return bytes([_AUTH_PROVE]) + session_proof(key, nonce)


def parse_prove_reply(data: bytes) -> bool:
    if data == bytes([_AUTH_PROVE, 1]):
        return True
    if data == bytes([_AUTH_PROVE, 0]):
        return False
    raise ValueError(f"unexpected proof reply {data.hex()}")
