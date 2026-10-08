# /// script
# requires-python = ">=3.9"
# dependencies = ["python-can"]
# ///
"""Guided tilt test: prints a timeline of pitch/roll while you tilt the sensor, to confirm the
axis map. Follow the prompts:

    0-5 s   flat and still
    5-10 s  lift the FRONT (arrow end of the case / GY-521 edge with the mounting holes)
    10-15 s flat
    15-20 s lift the DRIVER side (the "VCC" side)
    20-25 s flat

Expected: pitch goes positive while the front is up (low bits show R = rear low), roll goes
positive while the driver side is up (low bits show P = passenger low). If either is reversed
or swapped, fix AXIS_FORWARD / AXIS_DRIVER in src/config.h. Does not change any settings.

    uv run tools/hwtest/tilt_test.py [--channel can0]
"""
import argparse
import sys
import time

from common import *  # noqa: F403

PROMPTS = {0: "flat and still", 5: "LIFT THE FRONT", 10: "flat", 15: "LIFT THE DRIVER SIDE", 20: "flat"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--seconds", type=int, default=25)
    args = ap.parse_args()
    bus = open_bus(args.channel)
    sa = find_sensor(bus)
    if sa is None:
        print("no sensor status seen on the bus")
        return 1

    print(f"sensor at SA 0x{sa:02X}; starting in 3 s")
    time.sleep(3)
    t0 = time.time()
    next_req = 0.0
    last_print = -1
    prompted = set()
    latest = None
    while time.time() - t0 < args.seconds:
        now = time.time() - t0
        sec = int(now)
        if sec in PROMPTS and sec not in prompted:
            print(f"--- {sec:2d}s: {PROMPTS[sec]}")
            prompted.add(sec)
        if now >= next_req:  # status is only broadcast at 1 Hz when steady; ask for it twice a second
            request(bus, sa, DGN_STATUS)
            next_req += 0.5
        for t, d in frames(collect(bus, 0.05), DGN_STATUS, sa):
            latest = d
        if sec != last_print and latest is not None:
            p, r, bits = status_angles(latest)
            print(f"t={sec:2d}s  pitch {p:+6.2f}  roll {r:+6.2f}  low[DFPR]={bits}")
            last_print = sec
    bus.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
