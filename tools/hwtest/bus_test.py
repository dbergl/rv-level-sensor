# /// script
# requires-python = ">=3.9"
# dependencies = ["python-can"]
# ///
"""Automated RV-C bus checks against a running level sensor.

Needs a SocketCAN interface on the sensor's bus at 250 kbit/s, and the sensor held still
(the set-zero check needs it motionless).

WARNING: this changes the sensor's saved settings (pitch thresholds 2.0/1.0, pitch and roll
zero points, and it pushes the sensor to the next source address). Restore defaults afterwards
with `pio run -e esp32 -t erase` followed by an upload.

    uv run tools/hwtest/bus_test.py [--channel can0]
"""
import argparse
import sys

from common import *  # noqa: F403

results = []


def check(name, ok, detail=""):
    ok = bool(ok)
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))


def acks(msgs):
    return [d for t, d in frames(msgs, DGN_ACK | TOOL_SA)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--channel", default="can0")
    args = ap.parse_args()
    bus = open_bus(args.channel)

    sa = find_sensor(bus)
    check("status broadcast present", sa is not None, f"sensor SA=0x{sa:02X}" if sa is not None else "no 1FFEB seen")
    if sa is None:
        return 1

    # Periodic broadcasts.
    m = collect(bus, 6.2)
    st = frames(m, DGN_STATUS, sa)
    gaps = [round(b[0] - a[0], 2) for a, b in zip(st, st[1:])]
    check("status heartbeat ~1 s", len(st) >= 5 and all(0.9 < g < 1.15 for g in gaps), f"{len(st)} frames, gaps {gaps}")
    d = st[-1][1]
    p, r, bits = status_angles(d)
    check("status encoding sane", d[5:7] == b"\xff\xff" and d[7] == 1 and d[0] & 0xAA == 0,
          f"pitch {p:+.2f} roll {r:+.2f} low[DFPR]={bits}")
    dm = frames(m, DGN_DM_RV, sa)
    check("DM_RV broadcast, no fault", dm and dm[0][1] == bytes([0x05, 81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]),
          dm[0][1].hex(" ") if dm else "none")

    # Address claim request.
    request(bus, 0xFF, DGN_ADDRESS_CLAIM)
    cl = frames(collect(bus, 0.5), DGN_ADDRESS_CLAIM, sa)
    check("global request for ADDRESS_CLAIM answered", len(cl) == 1 and cl[0][1][7] & 0x80, cl[0][1].hex(" ") if cl else "none")

    # PRODUCT_ID (multi-packet).
    request(bus, sa, DGN_PRODUCT_ID)
    m = collect(bus, 1.0)
    init, data = frames(m, DGN_INITIAL_PACKET, sa), frames(m, DGN_DATA_PACKET, sa)
    ok = len(init) == 1 and init[0][1][0] == 0x20 and init[0][1][5:8] == bytes([0xEB, 0xFE, 0x00])
    length = init[0][1][1] | init[0][1][2] << 8 if init else 0
    payload = b"".join(x[1:] for t, x in data)[:length]
    spacing = [round(b[0] - a[0], 3) for a, b in zip(init + data, (init + data)[1:])]
    check("PRODUCT_ID multi-packet", ok and len(data) == init[0][1][3] and all(s >= 0.049 for s in spacing),
          f"'{payload.decode(errors='replace')}' spacing {spacing}")

    # DM_RV requests.
    request(bus, sa, DGN_DM_RV)
    check("directed DM_RV request answered", len(frames(collect(bus, 0.3), DGN_DM_RV, sa)) == 1)
    request(bus, 0xFF, DGN_DM_RV)
    check("global DM_RV request ignored (spec 3.2.4.3)", len(frames(collect(bus, 0.3), DGN_DM_RV, sa)) == 0)

    # Unsupported DGN.
    request(bus, sa, 0x1FFFF)
    a = acks(collect(bus, 0.3))
    check("unsupported directed request -> NAK 1",
          len(a) == 1 and a[0][0] == 1 and a[0][4] == TOOL_SA and a[0][5:8] == bytes([0xFF, 0xFF, 0x01]),
          a[0].hex(" ") if a else "none")
    request(bus, 0xFF, 0x1FFFF)
    check("unsupported global request -> silence", len(acks(collect(bus, 0.3))) == 0)

    # Config status on request.
    request(bus, sa, DGN_PITCH_CFG_STATUS)
    cs = frames(collect(bus, 0.3), DGN_PITCH_CFG_STATUS, sa)
    check("pitch config status on request", len(cs) == 1 and cs[0][1][0] == 1, cs[0][1].hex(" ") if cs else "none")

    # Threshold commands.
    send(bus, 6, DGN_PITCH_CFG_CMD, [1] + encode_angle(2.0) + encode_angle(1.0) + [0xFF, 0xFF, 0xFF])
    m = collect(bus, 0.4)
    cs = frames(m, DGN_PITCH_CFG_STATUS, sa)
    check("set pitch thresholds 2.0/1.0: no ACK, status updated",
          not acks(m) and len(cs) == 1 and cs[0][1][1:5] == bytes(encode_angle(2.0) + encode_angle(1.0)),
          cs[0][1].hex(" ") if cs else "none")
    send(bus, 6, DGN_PITCH_CFG_CMD, [1] + encode_angle(80.0) + [0xFF] * 5)
    a = acks(collect(bus, 0.4))
    check("threshold 80 deg -> NAK 5", len(a) == 1 and a[0][0] == 5 and a[0][5:8] == bytes([0xE8, 0xFD, 0x01]),
          a[0].hex(" ") if a else "none")
    send(bus, 6, DGN_PITCH_CFG_CMD, [7] + encode_angle(80.0) + [0xFF, 0xFF, 0xFD, 0xFF, 0xFF])
    m = collect(bus, 0.4)
    check("command for instance 7 ignored", not acks(m) and not frames(m, DGN_PITCH_CFG_STATUS, sa))

    # Set zero on both axes (sensor must be still).
    send(bus, 6, DGN_PITCH_CFG_CMD, [1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF])
    send(bus, 6, DGN_ROLL_CFG_CMD, [1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF])
    a = acks(collect(bus, 0.5))
    check("set-zero pitch + roll -> ACK 0 twice", len(a) == 2 and all(x[0] == 0 for x in a),
          " / ".join(x.hex(" ") for x in a) + ("  (NAK 3 = sensor was moving)" if any(x[0] == 3 for x in a) else ""))
    st = frames(collect(bus, 1.5), DGN_STATUS, sa)
    if st:
        p, r, bits = status_angles(st[-1][1])
        check("after zero: pitch/roll ~0, level bits clear", abs(p) < 0.1 and abs(r) < 0.1 and bits == "0000",
              f"pitch {p:+.3f} roll {r:+.3f} low[DFPR]={bits}")
    else:
        check("after zero: status received", False)

    # Address conflict: a higher-priority NAME (all zero) claims our address.
    send(bus, 6, DGN_ADDRESS_CLAIM, [0] * 8, sa=sa)
    m = collect(bus, 1.2)
    reqs = [parse(i)[1] & 0xFF for t, i, d in m if (parse(i)[1] & 0x1FF00) == DGN_REQUEST and parse(i)[2] == 0xFE]
    claims = [parse(i)[2] for t, i, d in m if parse(i)[1] == DGN_ADDRESS_CLAIM]
    new_sa = sa - 1
    check("higher-priority claim on our SA -> move to next address",
          reqs[:1] == [new_sa] and new_sa in claims and not frames(m, DGN_STATUS, sa),
          f"requests to {[hex(x) for x in reqs]}, claims from {[hex(x) for x in claims]}")
    check("traffic resumes on new SA", len(frames(collect(bus, 1.5), DGN_STATUS, new_sa)) >= 1)
    send(bus, 6, DGN_ADDRESS_CLAIM, [0xFF] * 8, sa=new_sa)
    check("lower-priority claim on our SA -> we re-assert", len(frames(collect(bus, 0.4), DGN_ADDRESS_CLAIM, new_sa)) == 1)

    print(f"\n{sum(results)}/{len(results)} passed; sensor now at SA 0x{new_sa:02X}. "
          "Saved settings were changed: erase + reflash to restore defaults.")
    bus.shutdown()
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
