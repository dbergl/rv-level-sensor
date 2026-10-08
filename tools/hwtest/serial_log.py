# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Captures the firmware's serial output, optionally resetting the board first so the boot
messages (e.g. an MPU-6050 init failure with its WHO_AM_I value) are included.

    uv run tools/hwtest/serial_log.py [--port /dev/ttyUSB0] [--seconds 12] [--no-reset]
"""
import argparse
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--seconds", type=float, default=12)
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()
    s = serial.Serial(args.port, 115200, timeout=0.2)
    if not args.no_reset:
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    t0 = time.time()
    while time.time() - t0 < args.seconds:
        data = s.read(4096)
        if data:
            print(data.decode(errors="replace"), end="", flush=True)


if __name__ == "__main__":
    main()
