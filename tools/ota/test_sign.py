import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("cryptography")
from cryptography.hazmat.primitives import serialization as ser  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: E402

import ota_proto as p  # noqa: E402

SIGN = Path(__file__).with_name("sign.py")
VECTOR_SIG = bytes.fromhex(
    "4678e098c9d4fa63833ed10ac0b7dad982f6d1860fb70a09b32e7bff31ec5bcb"
    "bb2745c3bb29e75418cdcd32fdcefe881697112edfa0b2fa4a8d6281403b4d00")


def run(tmp_path, *args):
    return subprocess.run([sys.executable, str(SIGN), *args], env={"OTA_KEY_DIR": str(tmp_path / "keys"),
                          "PATH": "/usr/bin:/bin"}, capture_output=True, text=True)


def test_vector_matches_firmware_tests():
    k = Ed25519PrivateKey.from_private_bytes(bytes(range(32)))
    img = bytes((i * 7 + 3) & 0xFF for i in range(1000))
    assert k.sign(p.signed_message(img)) == VECTOR_SIG


def test_keygen_sign_verify(tmp_path):
    assert run(tmp_path, "keygen").returncode == 0
    priv = tmp_path / "keys" / "ota_ed25519.pem"
    assert priv.stat().st_mode & 0o777 == 0o600
    assert len(bytes.fromhex((tmp_path / "keys" / "ota_ed25519.pub").read_text().strip())) == 32
    fw = tmp_path / "firmware.bin"
    fw.write_bytes(b"\x00" * 5000)
    assert run(tmp_path, "sign", str(fw)).returncode == 0
    assert len((tmp_path / "firmware.bin.sig").read_bytes()) == 64
    assert run(tmp_path, "verify", str(fw)).returncode == 0
    fw.write_bytes(b"\x01" * 5000)
    r = run(tmp_path, "verify", str(fw))
    assert r.returncode != 0 and "does NOT match" in r.stderr


def test_keygen_never_overwrites(tmp_path):
    assert run(tmp_path, "keygen").returncode == 0
    before = (tmp_path / "keys" / "ota_ed25519.pem").read_bytes()
    r = run(tmp_path, "keygen")
    assert r.returncode != 0 and "already exists" in r.stderr
    assert (tmp_path / "keys" / "ota_ed25519.pem").read_bytes() == before


def test_sign_without_key_explains(tmp_path):
    fw = tmp_path / "firmware.bin"
    fw.write_bytes(b"x")
    r = run(tmp_path, "sign", str(fw))
    assert r.returncode != 0 and "keygen" in r.stderr
