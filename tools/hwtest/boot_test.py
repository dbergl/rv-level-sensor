# /// script
# requires-python = ">=3.9"
# dependencies = ["python-can", "pyserial"]
# ///
"""Resets the sensor through its USB serial port and checks the power-up sequence on the bus:
request for ADDRESS_CLAIM from FEh, ADDRESS_CLAIM >= 250 ms later, then >= 250 ms of silence
before the first status (RV-C 4.3.1.2, 00D-R04). Then reads back both config statuses, which
shows whether saved settings survived the reset. Does not change any settings.

    uv run tools/hwtest/boot_test.py [--channel can0] [--port /dev/ttyUSB0]
"""
import argparse
import sys
import time

import serial

from common import *  # noqa: F403


def reset_board(port):
    s = serial.Serial(port, 115200)
    s.dtr = False
    s.rts = True  # EN low through the auto-reset circuit
    time.sleep(0.1)
    s.rts = False
    s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    args = ap.parse_args()
    bus = open_bus(args.channel)

    reset_board(args.port)
    msgs = collect(bus, 3.0)
    req = next(((t, i) for t, i, d in msgs if (parse(i)[1] & 0x1FF00) == DGN_REQUEST and parse(i)[2] == 0xFE), None)
    claim = next(((t, i) for t, i, d in msgs if parse(i)[1] == DGN_ADDRESS_CLAIM), None)
    if not req or not claim:
        print("FAIL: no address request/claim seen after reset")
        return 1
    sa = parse(claim[1])[2]
    status = next((t for t, i, d in msgs if parse(i)[1] == DGN_STATUS and parse(i)[2] == sa), None)
    t0 = req[0]
    for t, i, d in msgs[:8]:
        print(f"  +{(t - t0) * 1000:7.1f} ms  {i:08X}  {d.hex(' ')}")
    gap1 = (claim[0] - req[0]) * 1000
    gap2 = (status - claim[0]) * 1000 if status else -1
    ok = gap1 >= 245 and gap2 >= 245
    print(f"[{'PASS' if ok else 'FAIL'}] request -> claim {gap1:.0f} ms, claim -> first status {gap2:.0f} ms "
          f"(both must be >= 250 ms; 5 ms allowed for USB timestamp jitter), SA 0x{sa:02X}")

    for dgn, name in ((DGN_PITCH_CFG_STATUS, "pitch"), (DGN_ROLL_CFG_STATUS, "roll")):
        request(bus, sa, dgn)
        cs = frames(collect(bus, 0.4), dgn, sa)
        if cs:
            d = cs[0][1]
            print(f"  {name} config: out-of-level {angle(d[1] | d[2] << 8):.2f} deg, "
                  f"within-level {angle(d[3] | d[4] << 8):.2f} deg")
        else:
            print(f"  {name} config: no response")
    bus.shutdown()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
