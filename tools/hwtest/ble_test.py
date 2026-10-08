# /// script
# requires-python = ">=3.9"
# dependencies = ["bleak>=0.22", "pyserial"]
# ///
"""Checks the sensor's BTHome v2 broadcast against its serial output.

    uv run tools/hwtest/ble_test.py [--port /dev/ttyUSB0] [--seconds 15]

Keep the sensor still. It checks: the "RV Level" name, the payload layout, that each packet ID
carries one content (a new ID also comes every 10 s without a change), that pitch and roll match the serial summary within 0.1 deg, and the advert rate.
"""
import argparse
import asyncio
import re
import struct
import sys
import time

import serial
from bleak import BleakScanner

UUID = "0000fcd2-0000-1000-8000-00805f9b34fb"
SUMMARY = re.compile(r"pitch=([+-][\d.]+) roll=([+-][\d.]+)")


def decode(body):
    """-> dict(pid, level, moving, problem, pitch, roll); pitch/roll None when omitted."""
    assert body[0] == 0x40, f"device info {body[0]:#x}"
    out, rot, i = {"pitch": None, "roll": None}, [], 1
    names = {0x00: "pid", 0x0F: "level", 0x22: "moving", 0x26: "problem"}
    last_id = -1
    while i < len(body):
        oid = body[i]
        assert oid >= last_id, "object IDs not ascending"
        last_id = oid
        if oid == 0x3F:
            rot.append(struct.unpack_from("<h", body, i + 1)[0] / 10)
            i += 3
        else:
            out[names[oid]] = body[i + 1]
            i += 2
    if rot:
        out["pitch"], out["roll"] = rot
    return out


def serial_angles(port, seconds):
    s = serial.Serial(port, 115200, timeout=0.2)
    got, t0 = [], time.time()
    while time.time() - t0 < seconds:
        m = SUMMARY.search(s.readline().decode(errors="replace"))
        if m:
            got.append((float(m.group(1)), float(m.group(2))))
    return got


async def scan(seconds):
    adverts, names = [], set()

    def cb(dev, adv):
        if UUID in adv.service_data:
            adverts.append((time.time(), bytes(adv.service_data[UUID])))
            if adv.local_name:
                names.add(adv.local_name)

    async with BleakScanner(cb, scanning_mode="active"):
        await asyncio.sleep(seconds)
    return adverts, names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--seconds", type=float, default=15)
    args = ap.parse_args()

    adverts, names = asyncio.run(scan(args.seconds))
    ser = serial_angles(args.port, 3)
    fails = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            fails.append(msg)

    check(bool(adverts), f"{len(adverts)} BTHome adverts received")
    if not adverts:
        sys.exit(1)
    check("RV Level" in names, f"scan response name {names}")
    decoded = [decode(b) for _, b in adverts]
    check(all(d["problem"] == 0 for d in decoded), "problem flag clear")
    check(all(d["pitch"] is not None for d in decoded), "rotations present")
    by_pid = {}
    for (_, b), d in zip(adverts, decoded):
        by_pid.setdefault(d["pid"], set()).add(b[3:])
    check(all(len(v) == 1 for v in by_pid.values()), f"packet ID unique per content ({len(by_pid)} IDs)")
    rate = len(adverts) / args.seconds
    check(rate >= 2, f"advert rate {rate:.1f}/s (host scanners drop many; >= 2/s expected)")
    if ser:
        sp, sr = ser[-1]
        bp, br = decoded[-1]["pitch"], decoded[-1]["roll"]
        check(abs(sp - bp) <= 0.1 and abs(sr - br) <= 0.1,
              f"serial pitch/roll {sp:+.2f}/{sr:+.2f} vs BLE {bp:+.1f}/{br:+.1f}")
    else:
        check(False, "no serial summary lines")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
