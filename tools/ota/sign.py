#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["cryptography>=42"]
# ///
"""Signing key and signatures for Bluetooth firmware updates.

  uv run tools/ota/sign.py keygen               # new key pair in ~/.config/rv-level/
  uv run tools/ota/sign.py sign firmware.bin    # writes firmware.bin.sig
  uv run tools/ota/sign.py verify firmware.bin  # checks firmware.bin.sig against the public key

The firmware build embeds the public key (tools/ota_pubkey.py). Keep the private key on this
machine and back it up: anyone who has it can install firmware on the sensor, and without it
only a USB install can change the key.
"""
import argparse
import os
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization as ser
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

sys.path.insert(0, str(Path(__file__).parent))
import ota_proto as p  # noqa: E402

KEY_DIR = Path(os.environ.get("OTA_KEY_DIR", Path.home() / ".config" / "rv-level"))
PRIV = KEY_DIR / "ota_ed25519.pem"
PUB = KEY_DIR / "ota_ed25519.pub"


def keygen(_):
    if PRIV.exists():
        sys.exit(f"{PRIV} already exists; not overwriting it")
    KEY_DIR.mkdir(parents=True, exist_ok=True, mode=0o700)
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(ser.Encoding.PEM, ser.PrivateFormat.PKCS8, ser.NoEncryption())
    with os.fdopen(os.open(PRIV, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as f:
        f.write(pem)
    PUB.write_text(key.public_key().public_bytes(ser.Encoding.Raw, ser.PublicFormat.Raw).hex() + "\n")
    print(f"private key: {PRIV}  (back it up, keep it private)\npublic key:  {PUB}")


def load_private():
    try:
        key = ser.load_pem_private_key(PRIV.read_bytes(), password=None)
    except FileNotFoundError:
        sys.exit(f"no signing key at {PRIV}; create one with: uv run tools/ota/sign.py keygen")
    if not isinstance(key, Ed25519PrivateKey):
        sys.exit(f"{PRIV} is not an Ed25519 key")
    return key


def sign(a):
    image = Path(a.firmware).read_bytes()
    sig = load_private().sign(p.signed_message(image))
    out = Path(a.firmware + ".sig")
    out.write_bytes(sig)
    print(f"signed {a.firmware} ({len(image)} bytes) -> {out}")


def verify(a):
    image = Path(a.firmware).read_bytes()
    sig = Path(a.firmware + ".sig").read_bytes()
    pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(PUB.read_text().strip()))
    try:
        pub.verify(sig, p.signed_message(image))
    except InvalidSignature:
        sys.exit(f"signature does NOT match {PUB}")
    print("signature OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("keygen").set_defaults(fn=keygen)
    for name, fn in (("sign", sign), ("verify", verify)):
        s = sub.add_parser(name)
        s.add_argument("firmware")
        s.set_defaults(fn=fn)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
