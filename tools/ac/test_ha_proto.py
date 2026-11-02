import pytest
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

import ha_proto as p
import vectors as v

# From `uv run tools/ac/vectors.py` (also in test/test_ha_link and test/test_ha_control).
HA_PRIV = bytes(range(0x00, 0x20))
ESP_PUB = bytes.fromhex("358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254")
HA_PUB = bytes.fromhex("8f40c5adb68f25624ae5b214ea767a6ec94d829d3d7b5e1ad1ba6f3e2138285f")
PAIR_KEY = bytes.fromhex("c727107d2c582dd9c86f513150a56d1043ed571501b60a279fda3df22d108c45")
NONCE = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")
MSG = bytes.fromhex("01000000020036e461c8a09b15adb32d84cfb276225c")
SESSION_PROOF = bytes.fromhex("6c5786a795708b586a4e97f451f2d872")


def test_pair_message_and_key_match_the_vectors():
    ha = X25519PrivateKey.from_private_bytes(HA_PRIV)
    assert p.pair_msg(v.raw_pub(ha)) == b"\x01" + HA_PUB
    esp_pub, key = p.finish_pair(ha, b"\x01" + ESP_PUB)
    assert esp_pub == ESP_PUB
    assert key == PAIR_KEY


def test_pairing_refusal_and_low_order_reply():
    ha = X25519PrivateKey.from_private_bytes(HA_PRIV)
    with pytest.raises(p.ProtoError, match="refused"):
        p.finish_pair(ha, b"\x01")
    with pytest.raises(p.ProtoError):
        p.finish_pair(ha, b"\x01" + bytes(32))


def test_session_reply():
    assert p.session_msg() == b"\x02"
    assert p.parse_session(b"\x02" + NONCE) == NONCE
    with pytest.raises(p.ProtoError, match="refused"):
        p.parse_session(b"\x02")


def test_command_message_matches_the_vector():
    # body 02 00: op 2 (fan), value 0 (auto)
    assert p.command_msg(PAIR_KEY, NONCE, 1, p.OPS["fan"], 0) == MSG


def test_negative_value_is_signed():
    m = p.command_msg(PAIR_KEY, NONCE, 3, p.OPS["step"], -1)
    assert m[:6] == b"\x03\x00\x00\x00\x04\xff"


def test_op_names():
    assert p.op_code("clear_preset") == 9
    assert p.op_code("3") == 3
    with pytest.raises(ValueError):
        p.op_code("heat")


def test_result_and_status_parsing():
    assert p.parse_result(b"\x07\x00\x00\x00\x04") == (7, "BadAuth")
    assert p.parse_status(b"") == {}
    d = bytes([1, 0x03, 0, 0, 72, 75, 13, 0x2C, 0x01]) + bytes(21) + bytes(4)
    s = p.parse_status(d)
    assert s["responding"] and s["power"] and not s["celsius"]
    assert (s["mode"], s["fan"], s["setpoint_f"], s["lvp_v"]) == ("cool", 0, 72, 30.0)


def test_prove_message_matches_the_vector():
    assert p.prove_msg(PAIR_KEY, NONCE) == b"\x03" + SESSION_PROOF
    p.parse_prove(b"\x03\x01")
    with pytest.raises(p.ProtoError, match="refused"):
        p.parse_prove(b"\x03\x00")
