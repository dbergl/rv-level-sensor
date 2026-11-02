"""Wire format of the AC bridge's Home Assistant service (lib/acbridge/ha_link, ha_control).

The key derivation and command MAC come from vectors.py, so the client and the shared test
vectors cannot drift apart.
"""
import struct

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey

from vectors import command_mac, pair_key, raw_pub, session_proof

SERVICE = "af480101-29e2-47f0-a341-4f1a49271bf4"
AUTH = "af480102-29e2-47f0-a341-4f1a49271bf4"
STATUS = "af480103-29e2-47f0-a341-4f1a49271bf4"
COMMAND = "af480104-29e2-47f0-a341-4f1a49271bf4"

PAIR = 0x01
SESSION = 0x02  # challenge request
PROVE = 0x03
STATUS_LEN = 34
RESULT_LEN = 5

# ac::Op
OPS = {
    "power": 0, "mode": 1, "fan": 2, "setpoint": 3, "step": 4, "display": 5, "oscillate": 6,
    "light": 7, "celsius": 8, "clear_preset": 9,
}
# cm::Mode
MODES = ["cool", "eco", "sleep", "turbo", "fan", "dehumid", "unknown"]
# ac::Result
RESULTS = ["Ok", "Refused", "Unreachable", "NotConfirmed", "BadAuth"]


class ProtoError(Exception):
    pass


def pair_msg(ha_pub: bytes) -> bytes:
    if len(ha_pub) != 32:
        raise ValueError("X25519 public key must be 32 bytes")
    return bytes([PAIR]) + ha_pub


def finish_pair(ha: X25519PrivateKey, reply: bytes) -> tuple[bytes, bytes]:
    """(esp_pub, key) from the ESP32's pairing reply. Raises ProtoError if it refused."""
    if reply[:1] != bytes([PAIR]):
        raise ProtoError(f"unexpected auth reply {reply.hex()}")
    if len(reply) == 1:
        raise ProtoError("pairing refused (is the pairing window open? 'pair' on the console)")
    if len(reply) != 33:
        raise ProtoError(f"bad pairing reply length {len(reply)}")
    esp_pub = reply[1:]
    try:
        shared = ha.exchange(X25519PublicKey.from_public_bytes(esp_pub))
    except ValueError as e:  # all-zero shared secret: low-order key
        raise ProtoError(f"ESP32 public key rejected: {e}") from None
    return esp_pub, pair_key(shared, raw_pub(ha), esp_pub)


def session_msg() -> bytes:
    return bytes([SESSION])


def parse_session(reply: bytes) -> bytes:
    """The challenge nonce. Raises ProtoError if refused (not paired)."""
    if reply[:1] != bytes([SESSION]):
        raise ProtoError(f"unexpected auth reply {reply.hex()}")
    if len(reply) == 1:
        raise ProtoError("session refused: the ESP32 has no pairing key (run 'pair' during the window)")
    if len(reply) != 17:
        raise ProtoError(f"bad session reply length {len(reply)}")
    return reply[1:]


def prove_msg(key: bytes, nonce: bytes) -> bytes:
    """03 || proof16: proves the pairing key for this link's challenge; the link becomes HA's session."""
    return bytes([PROVE]) + session_proof(key, nonce)


def parse_prove(reply: bytes) -> None:
    if reply == bytes([PROVE, 1]):
        return
    if reply == bytes([PROVE, 0]):
        raise ProtoError("session proof refused: wrong pairing key (re-pair) or no challenge")
    raise ProtoError(f"unexpected auth reply {reply.hex()}")


def op_code(op: str) -> int:
    if op in OPS:
        return OPS[op]
    try:
        return int(op, 0)
    except ValueError:
        raise ValueError(f"unknown op {op!r} (one of {', '.join(OPS)} or a number)") from None


def command_msg(key: bytes, nonce: bytes, counter: int, op: int, value: int) -> bytes:
    """counter (u32 LE) || op u8 || value i8 || mac16."""
    body = struct.pack("<Bb", op, value)
    return struct.pack("<I", counter) + body + command_mac(key, nonce, counter, body)


def parse_result(d: bytes) -> tuple[int, str]:
    if len(d) != RESULT_LEN:
        raise ProtoError(f"bad result length {len(d)}")
    counter, r = struct.unpack("<IB", d)
    return counter, RESULTS[r] if r < len(RESULTS) else f"result {r}"


def parse_status(d: bytes) -> dict:
    """packStatus layout; {} for an empty read (no authenticated session)."""
    if len(d) == 0:
        return {}
    if len(d) != STATUS_LEN:
        raise ProtoError(f"bad status length {len(d)}")
    flags = d[1]
    return {
        "version": d[0],
        "responding": bool(flags & 0x01),
        "power": bool(flags & 0x02),
        "display": bool(flags & 0x04),
        "light": bool(flags & 0x08),
        "oscillating": bool(flags & 0x10),
        "celsius": bool(flags & 0x20),
        "mode": MODES[d[2]] if d[2] < len(MODES) else d[2],
        "fan": d[3],
        "setpoint_f": d[4],
        "air_f": d[5],
        "battery_v": d[6],
        "lvp_v": struct.unpack("<H", d[7:9])[0] / 10,
        "raw": d[9:30].hex(),
    }
