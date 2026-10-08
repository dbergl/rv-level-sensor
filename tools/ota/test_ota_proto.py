import hashlib
import struct

import ota_proto as p


def test_begin_cmd_layout():
    img = bytes(range(256)) * 3
    cmd = p.begin_cmd(img)
    assert len(cmd) == 37
    assert cmd[0] == p.BEGIN
    assert struct.unpack("<I", cmd[1:5])[0] == len(img)
    assert cmd[5:] == hashlib.sha256(img).digest()


def test_data_write_layout():
    assert p.data_write(0x01020304, b"xy") == b"\x04\x03\x02\x01xy"


def test_parse_notice():
    assert p.parse_notice(b"\x01\x00\x10\x00\x00") == ("PROGRESS", 4096)
    assert p.parse_notice(bytearray(b"\x16\x08\x15\x00\x00")) == ("ERR_FLASH", 0x1508)
    assert p.parse_notice(b"\x42\x00\x00\x00\x00") == ("0x42", 0)


def test_parse_info():
    raw = b"\x01\x01\x2c\x01" + b"abc1234 2026-09-27 10:00:00"
    assert p.parse_info(raw) == {"state": "on trial", "slot": 1, "window_s": 300,
                                 "version": "abc1234 2026-09-27 10:00:00"}
    assert p.parse_info(b"\x00\x00\x00\x00v")["state"] == "confirmed"


def test_general_reset_enter_bootloader_bytes():
    d = p.general_reset_enter_bootloader()
    assert len(d) == 8
    assert d[0] == 0xFF                   # byte 0: no action in any field
    assert (d[1] >> 4) & 3 == 1           # byte 1 bits 4-5: Reboot/Enter Bootloader
    assert d[1] | 0x30 == 0xFF and d[1] & 0x30 == 0x10


def test_sender_stays_within_window():
    s = p.Sender(size=100000, chunk=500, window=16384)
    sent = 0
    while (w := s.next_write()) is not None:
        off, n = w
        assert off == sent
        sent += n
    assert sent == 16500  # stops once 16384 or more is unacknowledged
    s.on_progress(4096)
    assert s.next_write() == (16500, 500)


def test_sender_last_chunk_and_done():
    s = p.Sender(size=1200, chunk=500, window=16384)
    assert [s.next_write() for _ in range(4)] == [(0, 500), (500, 500), (1000, 200), None]
    assert not s.done
    s.on_progress(1200)
    assert s.done


def test_sender_resend_rewinds():
    s = p.Sender(size=3000, chunk=500, window=16384)
    for _ in range(4):
        s.next_write()
    s.on_resend(500)
    assert s.next_write() == (500, 500)
    assert s.acked == 500


def test_flash_hint():
    assert "on trial" in p.flash_hint(0x1508)
    assert p.flash_hint(0x101) == ""


def test_chunk_size_fits_one_attribute():
    # A data write (4-byte offset + chunk) must fit in one ATT write (MTU - 3) and in NimBLE's
    # 512-byte attribute limit; a longer write-without-response is silently dropped.
    assert p.chunk_size(517) == 508
    assert p.chunk_size(247) == 240
    assert p.chunk_size(23) == 16
