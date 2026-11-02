#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["cryptography>=42"]
# ///
"""Fixed test vectors for AC bridge pairing and command authentication (lib/acbridge/ha_link).

  uv run tools/ac/vectors.py

Deterministic: fixed private keys and nonce, so the C++ and Python tests can share the output.
"""
import hashlib
import struct

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

PAIR_LABEL = b"RVL-AC-PAIR-v1"
CMD_LABEL = b"RVL-AC-CMD-v1"
SESSION_LABEL = b"RVL-AC-SESSION-v1"


def raw_pub(k: X25519PrivateKey) -> bytes:
    return k.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def pair_key(shared: bytes, ha_pub: bytes, esp_pub: bytes) -> bytes:
    return hashlib.blake2b(PAIR_LABEL + ha_pub + esp_pub, key=shared, digest_size=32).digest()


def command_mac(key: bytes, nonce: bytes, counter: int, body: bytes) -> bytes:
    return hashlib.blake2b(CMD_LABEL + nonce + struct.pack("<I", counter) + body, key=key, digest_size=16).digest()


def session_proof(key: bytes, nonce: bytes) -> bytes:
    return hashlib.blake2b(SESSION_LABEL + nonce, key=key, digest_size=16).digest()


def main() -> None:
    ha_priv = bytes(range(0x00, 0x20))
    esp_priv = bytes(range(0x20, 0x40))
    ha = X25519PrivateKey.from_private_bytes(ha_priv)
    esp = X25519PrivateKey.from_private_bytes(esp_priv)
    ha_pub, esp_pub = raw_pub(ha), raw_pub(esp)
    shared = ha.exchange(X25519PublicKey.from_public_bytes(esp_pub))
    assert shared == esp.exchange(X25519PublicKey.from_public_bytes(ha_pub))
    key = pair_key(shared, ha_pub, esp_pub)

    nonce = bytes(range(0xA0, 0xB0))
    counter = 1
    body = bytes([0x02, 0x00])
    mac = command_mac(key, nonce, counter, body)
    msg = struct.pack("<I", counter) + body + mac

    for name, v in [
        ("ha_priv", ha_priv), ("esp_priv", esp_priv), ("ha_pub", ha_pub), ("esp_pub", esp_pub),
        ("shared", shared), ("pair_key", key), ("nonce", nonce),
    ]:
        print(f"{name} = {v.hex()}")
    print(f"counter = {counter}")
    print(f"body = {body.hex()}")
    print(f"msg = {msg.hex()}")
    print(f"mac = {mac.hex()}")
    print(f"session_proof = {session_proof(key, nonce).hex()}")


if __name__ == "__main__":
    main()
