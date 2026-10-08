"""Bluetooth firmware update protocol, host side
(docs/protocol.md). Pure: no Bluetooth or CAN here."""
import hashlib
import struct

SERVICE = "af480001-29e2-47f0-a341-4f1a49271bf4"
INFO = "af480002-29e2-47f0-a341-4f1a49271bf4"
CONTROL = "af480003-29e2-47f0-a341-4f1a49271bf4"
DATA = "af480004-29e2-47f0-a341-4f1a49271bf4"

DOMAIN = b"RV-Level OTA v1\x00"  # first 16 bytes of the signed message (docs/protocol.md)

BEGIN, FINISH, ABORT, CLOSE = 0x01, 0x02, 0x03, 0x04

CODES = {
    0x00: "OK", 0x01: "PROGRESS", 0x02: "RESEND", 0x03: "DONE",
    0x10: "ERR_WINDOW_CLOSED", 0x11: "ERR_BAD_SIZE", 0x12: "ERR_NOT_STARTED", 0x13: "ERR_INCOMPLETE",
    0x14: "ERR_HASH", 0x15: "ERR_INVALID_IMAGE", 0x16: "ERR_FLASH", 0x17: "ERR_BAD_COMMAND", 0x18: "ERR_SIGNATURE",
}

WINDOW_BYTES = 16384  # most data in flight beyond the last PROGRESS
ATTR_MAX = 512  # NimBLE's (and ATT's) largest attribute value; longer writes are dropped


def chunk_size(mtu: int) -> int:
    """Image bytes per data write: offset (4) + chunk fits one ATT write and one attribute."""
    return min(mtu - 3, ATTR_MAX) - 4


def signed_message(image: bytes) -> bytes:
    """What the Ed25519 signature covers: domain, image size (u32 LE), SHA-256 of the image."""
    return DOMAIN + struct.pack("<I", len(image)) + hashlib.sha256(image).digest()


def begin_cmd(image: bytes, signature: bytes) -> bytes:
    if len(signature) != 64:
        raise ValueError(f"signature must be 64 bytes, not {len(signature)}")
    return unsigned_begin_cmd(image) + signature


def unsigned_begin_cmd(image: bytes) -> bytes:
    """BEGIN for firmware that predates signed updates; signing firmware refuses it."""
    return struct.pack("<BI", BEGIN, len(image)) + hashlib.sha256(image).digest()


def data_write(offset: int, chunk: bytes) -> bytes:
    return struct.pack("<I", offset) + chunk


def parse_notice(raw) -> tuple:
    code, value = struct.unpack("<BI", bytes(raw))
    return CODES.get(code, f"0x{code:02X}"), value


def parse_info(raw) -> dict:
    raw = bytes(raw)
    state, slot, secs = struct.unpack("<BBH", raw[:4])
    return {"state": "on trial" if state else "confirmed", "slot": slot, "window_s": secs,
            "version": raw[4:].decode("ascii", "replace")}


def general_reset_enter_bootloader() -> bytes:
    """GENERAL_RESET payload: byte 1 bits 4-5 = 01b (Reboot/Enter Bootloader), every other field 11b."""
    return bytes([0xFF, 0xDF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])


def flash_hint(err: int) -> str:
    if err == 0x1508:  # ESP_ERR_OTA_ROLLBACK_INVALID_STATE
        return "the running firmware is still on trial; wait a minute for it to confirm, then retry"
    return ""


def signature_hint(value: int) -> str:
    if value == 0:
        return "the sensor refuses unsigned images; sign it with tools/ota/sign.py"
    return "the signature does not match the key built into the sensor's firmware"


def begin_hint(name: str, value: int) -> str:
    """Reply to a signed BEGIN from firmware that predates signing (it expects 37 bytes)."""
    if name == "ERR_BAD_COMMAND" and value == 0:
        return "the sensor's firmware predates signed updates: install this build once with --unsigned"
    return ""


class Sender:
    """Chooses the next data write: stays within `window` bytes of the last PROGRESS and rewinds on RESEND."""

    def __init__(self, size: int, chunk: int, window: int = WINDOW_BYTES):
        self.size, self.chunk, self.window = size, chunk, window
        self.next = 0
        self.acked = 0

    def next_write(self):
        """(offset, length) to send now, or None to wait for a notification."""
        if self.next >= self.size or self.next - self.acked >= self.window:
            return None
        n = min(self.chunk, self.size - self.next)
        off = self.next
        self.next += n
        return off, n

    def on_progress(self, received: int):
        self.acked = max(self.acked, received)

    def on_resend(self, offset: int):
        self.next = offset
        self.acked = offset  # the sensor has exactly `offset` bytes

    @property
    def done(self) -> bool:
        return self.acked >= self.size
