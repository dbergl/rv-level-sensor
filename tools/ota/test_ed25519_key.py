import pytest

import ed25519_key as k

REAL = bytes.fromhex("03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8")
L = 2**252 + 27742317777372353535851937790883648493


def test_accepts_the_real_key():
    assert k.check_public_key(REAL) == ""


def test_accepts_a_generated_key():
    ed = pytest.importorskip("cryptography.hazmat.primitives.asymmetric.ed25519")
    ser = pytest.importorskip("cryptography.hazmat.primitives.serialization")
    raw = ed.Ed25519PrivateKey.generate().public_key().public_bytes(
        ser.Encoding.Raw, ser.PublicFormat.Raw)
    assert k.check_public_key(raw) == ""


@pytest.mark.parametrize("key", [
    b"\x01" + bytes(31),    # the identity point
    bytes(32),              # y = 0, which has order 4
    b"\xff" * 32,           # y >= p: not canonical
    REAL[:31],              # wrong length
    REAL + b"\x00",
])
def test_rejects(key):
    assert k.check_public_key(key) != ""


def test_rejects_order_8_points():
    found = set()
    for y in range(2, 40):
        p = k.decode_point(y.to_bytes(32, "little"))
        if p is None:
            continue
        t = k.scalar_mult(L, p)  # clears the large-prime part, leaving a point of order dividing 8
        if k.encode_point(k.scalar_mult(4, t)) != k.encode_point(k.IDENTITY):  # so order exactly 8
            found.add(k.encode_point(t))
    assert len(found) >= 2
    for enc in found:
        assert k.check_public_key(enc) != ""


def test_rejects_negative_zero_x():
    assert k.check_public_key(b"\x01" + bytes(30) + b"\x80") != ""
