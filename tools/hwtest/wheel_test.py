# /// script
# requires-python = ">=3.9"
# dependencies = ["python-can"]
# ///
"""RV-C checks for the block-leveling messages: LEVEL_WHEEL_STATUS (1EFBFh) and
LEVEL_CONFIG_COMMAND (proprietary-A2 1EF00h, signature "LV").

Needs a SocketCAN interface on the sensor's bus at 250 kbit/s. It changes the block
configuration while it runs and puts back the configuration it found, even if it fails.

    uv run tools/hwtest/wheel_test.py [--channel can0]
"""
import argparse
import sys

from common import *  # noqa: F403

DEFAULTS = bytes([0x04, 0x36, 0x06, 0xAD, 0x02, 0x14, 0x0A, 0x19])
FILL = [0xFF] * 5
results = []


def check(name, ok, detail=""):
    ok = bool(ok)
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))


def records(msgs, sa, rec):
    return [(t, d) for t, d in frames(msgs, DGN_WHEEL, sa) if d[0] == rec]


def command(bus, body):
    send(bus, 6, DGN_PROP_A2 | 0xFF, [0x4C, 0x56] + body)


def last_config(bus, sa, secs=0.1):
    """Payload of the last record 04h seen within `secs`, or None."""
    r = records(collect(bus, secs), sa, 4)
    return r[-1][1] if r else None


def hexs(d):
    return d.hex(" ") if d else "none"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--channel", default="can0")
    args = ap.parse_args()
    bus = open_bus(args.channel)

    sa = find_sensor(bus)
    check("sensor found", sa is not None, f"SA=0x{sa:02X}" if sa is not None else "no 1FFEB seen")
    if sa is None:
        return 1

    # Heartbeat: every record at least once a second.
    m = collect(bus, 3.2)
    counts = {r: len(records(m, sa, r)) for r in (1, 2, 3, 4)}
    check("all four records at least once a second", all(c >= 3 for c in counts.values()), f"counts {counts}")
    ids = {i for t, i, d in m if parse(i)[1] == DGN_WHEEL}
    check("CAN ID 19EFBF<SA>", ids == {0x19EFBF00 | sa}, str([hex(i) for i in ids]))
    r1, r2, r3 = (records(m, sa, r)[-1][1] if records(m, sa, r) else None for r in (1, 2, 3))
    check("record 01h: no sensor fault", r1 and (r1[5] >> 4) & 3 == 0, hexs(r1))
    check("records 02h/03h: reserved bytes FFh", r2 and r3 and r2[5:] == b"\xff" * 3 and r3[5:] == b"\xff" * 3,
          f"{hexs(r2)} / {hexs(r3)}")

    # Config command round trip. Keep the owner's configuration to put back at the end.
    request(bus, sa, DGN_WHEEL)
    saved = last_config(bus, sa, 0.3)
    check("current configuration read", saved is not None, hexs(saved))
    if saved is None:
        return 1
    try:
        config_checks(bus, sa)
    finally:
        command(bus, [0x01, saved[1], saved[2], saved[3], saved[4], 0xFF])
        command(bus, [0x02, saved[5], saved[6], saved[7], 0xFF, 0xFF])
        c = last_config(bus, sa, 0.3)
        check("configuration restored", c == saved, hexs(c))

    print(f"\n{sum(results)}/{len(results)} passed")
    bus.shutdown()
    return 0 if all(results) else 1


def config_checks(bus, sa):
    command(bus, [0x05] + FILL)
    c = last_config(bus, sa)
    check("restore defaults -> record 04h echoes the defaults", c == DEFAULTS, hexs(c))
    command(bus, [0x02, 0x14, 0x0C, 0x19, 0xFF, 0xFF])
    c = last_config(bus, sa)
    check("set blocks (1.00 in, 12, 0.25 in) echoed within 100 ms", c and c[5:8] == bytes([0x14, 0x0C, 0x19]), hexs(c))
    command(bus, [0x03] + FILL)
    c = last_config(bus, sa)
    check("blocks carried +1 -> 13", c and c[6] == 13, hexs(c))
    command(bus, [0x04] + FILL)
    c = last_config(bus, sa)
    check("blocks carried -1 -> 12", c and c[6] == 12, hexs(c))

    # Not "LV", and addressed to the sensor only: never broadcast another device's command on a live bus.
    send(bus, 6, DGN_PROP_A2 | sa, [0x00, 0x00, 0x02, 0x28, 0x03, 0x19, 0xFF, 0xFF])
    c = last_config(bus, sa, 1.2)  # wait for a heartbeat 04h
    check("frame without the LV signature changes nothing", c and c[5:8] == bytes([0x14, 0x0C, 0x19]), hexs(c))
    command(bus, [0x01, 0x88, 0x13, 0xFF, 0xFF, 0xFF])  # wheelbase 500.0 in: out of range
    c = last_config(bus, sa)
    check("out-of-range wheelbase rejected, echo unchanged", c and c[1:3] == bytes([0x36, 0x06]), hexs(c))

    # The panel's LEVEL SETUP geometry steppers: +/-0.5 in, from the defaults (159.0 / 68.5 in).
    for op, want, what in ((0x0B, (1595, 685), "wheelbase +0.5 -> 159.5 in"),
                           (0x0C, (1590, 685), "wheelbase -0.5 -> 159.0 in"),
                           (0x0D, (1590, 690), "track +0.5 -> 69.0 in"),
                           (0x0E, (1590, 685), "track -0.5 -> 68.5 in")):
        command(bus, [op] + FILL)
        c = last_config(bus, sa)
        got = (c[1] | c[2] << 8, c[3] | c[4] << 8) if c else None
        check(what, got == want, hexs(c))

    # REQUEST for 1EFBFh.
    request(bus, 0xFF, DGN_WHEEL)
    got = sorted({d[0] for t, d in frames(collect(bus, 0.2), DGN_WHEEL, sa)})
    check("global request -> all four records", got == [1, 2, 3, 4], str(got))
    request(bus, sa, DGN_WHEEL)
    got = sorted({d[0] for t, d in frames(collect(bus, 0.2), DGN_WHEEL, sa)})
    check("directed request -> all four records", got == [1, 2, 3, 4], str(got))


if __name__ == "__main__":
    sys.exit(main())
