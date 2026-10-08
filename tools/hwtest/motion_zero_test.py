# /// script
# requires-python = ">=3.9"
# dependencies = ["python-can", "pyserial"]
# ///
"""Checks that set-zero is refused while the sensor is moving. Watches the firmware's serial
summary and, the moment it reports moving=1, sends a pitch set-zero command, which must be
answered with NAK 3 ("conditions do not allow"). Start rocking the sensor any time within
20 s of launching. A refused command changes nothing.

    uv run tools/hwtest/motion_zero_test.py [--channel can0] [--port /dev/ttyUSB0]
"""
import argparse
import re
import sys
import time

import serial

from common import *  # noqa: F403

SET_ZERO_PITCH = [1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--seconds", type=float, default=20)
    args = ap.parse_args()
    bus = open_bus(args.channel)
    s = serial.Serial(args.port, 115200, timeout=0.1)
    s.reset_input_buffer()

    print("start rocking the sensor...")
    t0, buf, results = time.time(), b"", []
    while time.time() - t0 < args.seconds and len(results) < 2:
        buf += s.read(512)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.decode(errors="replace").strip()
            m = re.search(r"moving=(\d)", line)
            if not line.startswith("SA=") or not m:
                continue
            print(f"t={time.time() - t0:5.1f}s  {line}")
            if m.group(1) == "1":
                send(bus, 6, DGN_PITCH_CFG_CMD, SET_ZERO_PITCH)
                a = [d for t, d in frames(collect(bus, 0.5), DGN_ACK | TOOL_SA)]
                code = a[0][0] if a else None
                results.append(code == 3)
                print(f"   set-zero while moving -> {('code %d' % code) if code is not None else 'no response'}"
                      f" [{'PASS' if code == 3 else 'FAIL'}]")
    bus.shutdown()
    if not results:
        print("FAIL: the sensor never reported moving=1")
        return 1
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
