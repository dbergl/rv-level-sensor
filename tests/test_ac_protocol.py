import struct

import pytest
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

from custom_components.rv_level.ac_protocol import (
    AUTH_UUID,
    COMMAND_UUID,
    SERVICE_UUID,
    STATUS_UUID,
    Mode,
    Op,
    Result,
    command_message,
    pair_key,
    pair_request,
    parse_pair_reply,
    parse_prove_reply,
    parse_session_reply,
    prove_request,
    session_proof,
    session_request,
    unpack_result,
    unpack_status,
)

# tools/ac/vectors.py
HA_PRIV = bytes(range(0x00, 0x20))
ESP_PUB = bytes.fromhex("358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254")
HA_PUB = bytes.fromhex("8f40c5adb68f25624ae5b214ea767a6ec94d829d3d7b5e1ad1ba6f3e2138285f")
PAIR_KEY = bytes.fromhex("c727107d2c582dd9c86f513150a56d1043ed571501b60a279fda3df22d108c45")
NONCE = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")
MSG = bytes.fromhex("01000000020036e461c8a09b15adb32d84cfb276225c")
PROOF = bytes.fromhex("6c5786a795708b586a4e97f451f2d872")


def _status(flags=0, mode=0, fan=0, sp=0, air=0, batt=0, lvp=0, raw=bytes(21), version=1):
    return bytes([version, flags, mode, fan, sp, air, batt]) + struct.pack("<H", lvp) + raw + bytes(4)


def test_uuids():
    assert SERVICE_UUID == "af480101-29e2-47f0-a341-4f1a49271bf4"
    assert AUTH_UUID == "af480102-29e2-47f0-a341-4f1a49271bf4"
    assert STATUS_UUID == "af480103-29e2-47f0-a341-4f1a49271bf4"
    assert COMMAND_UUID == "af480104-29e2-47f0-a341-4f1a49271bf4"


def test_enums():
    assert Op.ClearPreset == 9 and Op.Celsius == 8 and Op.Power == 0
    assert Mode.Unknown == 6 and Mode.FanOnly == 4
    assert Result.BadAuth == 4


def test_pair_key_vector():
    priv = X25519PrivateKey.from_private_bytes(HA_PRIV)
    assert pair_key(priv, ESP_PUB) == PAIR_KEY


def test_command_message_vector():
    assert command_message(PAIR_KEY, NONCE, 1, Op.Fan, 0) == MSG


def test_session_proof_vector():
    assert session_proof(PAIR_KEY, NONCE) == PROOF


def test_auth_helpers():
    assert pair_request(HA_PUB) == b"\x01" + HA_PUB
    with pytest.raises(ValueError):
        pair_request(b"short")
    assert parse_pair_reply(b"\x01" + ESP_PUB) == ESP_PUB
    assert parse_pair_reply(b"\x01") is None
    with pytest.raises(ValueError):
        parse_pair_reply(b"\x02" + ESP_PUB)
    with pytest.raises(ValueError):
        parse_pair_reply(b"\x01abc")
    assert session_request() == b"\x02"
    assert parse_session_reply(b"\x02" + NONCE) == NONCE
    assert parse_session_reply(b"\x02") is None
    with pytest.raises(ValueError):
        parse_session_reply(b"\x03" + NONCE)
    with pytest.raises(ValueError):
        parse_session_reply(b"\x02abc")
    assert prove_request(PAIR_KEY, NONCE) == b"\x03" + PROOF
    assert parse_prove_reply(b"\x03\x01") is True
    assert parse_prove_reply(b"\x03\x00") is False
    with pytest.raises(ValueError):
        parse_prove_reply(b"\x03\x02")
    with pytest.raises(ValueError):
        parse_prove_reply(b"")


def test_unpack_status_fields():
    raw = bytes(range(21))
    s = unpack_status(_status(flags=0b101011, mode=3, fan=4, sp=72, air=80, batt=125, lvp=0x0100 + 0x7B, raw=raw))
    assert (s.responding, s.power, s.display, s.light, s.oscillating, s.celsius) == (
        True, True, False, True, False, True)
    assert s.mode is Mode.Turbo
    assert (s.fan, s.setpoint_f, s.air_temp_f, s.battery_v) == (4, 72, 80, 125)
    assert s.low_voltage_protection == (256 + 0x7B) / 10
    assert s.raw == raw


def test_unpack_status_unknown_is_none():
    s = unpack_status(_status(mode=6, fan=0xFF, sp=0xFF, air=0xFF, batt=0xFF))
    assert s.mode is Mode.Unknown
    assert s.fan is None and s.setpoint_f is None and s.air_temp_f is None and s.battery_v is None
    assert unpack_status(_status(fan=0)).fan == 0  # auto, not unknown


def test_unpack_status_rejects_bad_payload():
    with pytest.raises(ValueError):
        unpack_status(b"")
    with pytest.raises(ValueError):
        unpack_status(_status()[:-1])
    with pytest.raises(ValueError):
        unpack_status(_status() + b"\0")
    with pytest.raises(ValueError):
        unpack_status(_status(version=2))
    with pytest.raises(ValueError):
        unpack_status(_status(mode=7))


def test_unpack_result():
    assert unpack_result(struct.pack("<IB", 7, 3)) == (7, Result.NotConfirmed)
    with pytest.raises(ValueError):
        unpack_result(b"\0" * 4)
    with pytest.raises(ValueError):
        unpack_result(struct.pack("<IB", 7, 9))
