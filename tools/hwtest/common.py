"""Shared helpers for the RV-C level sensor bench tests (SocketCAN via python-can)."""
import time

import can

TOOL_SA = 0x80  # source address the test tool transmits from

DGN_REQUEST = 0x0EA00
DGN_ACK = 0x0E800
DGN_ADDRESS_CLAIM = 0x0EE00
DGN_INITIAL_PACKET = 0x0ECFF
DGN_DATA_PACKET = 0x0EBFF
DGN_PRODUCT_ID = 0x0FEEB
DGN_DM_RV = 0x1FECA
DGN_STATUS = 0x1FFEB
DGN_PITCH_CFG_STATUS = 0x1FDE9
DGN_PITCH_CFG_CMD = 0x1FDE8
DGN_ROLL_CFG_STATUS = 0x1FDEB
DGN_ROLL_CFG_CMD = 0x1FDEA
DGN_WHEEL = 0x1EFBF  # LEVEL_WHEEL_STATUS: proprietary-A2, always to BFh
DGN_PROP_A2 = 0x1EF00  # proprietary-A2; OR in the destination. Carries LEVEL_CONFIG_COMMAND
DGN_GENERAL_RESET = 0x17F00  # destination-specific; OR in the destination


def open_bus(channel):
    return can.Bus(interface="socketcan", channel=channel)


def ident(prio, dgn, sa):
    return (prio << 26) | (dgn << 8) | sa


def parse(arbitration_id):
    """-> (priority, dgn, source address)"""
    return (arbitration_id >> 26) & 7, (arbitration_id >> 8) & 0x1FFFF, arbitration_id & 0xFF


def send(bus, prio, dgn, data, sa=TOOL_SA):
    bus.send(can.Message(arbitration_id=ident(prio, dgn, sa), data=bytes(data), is_extended_id=True))


def request(bus, dest, dgn, instance=0xFF):
    send(bus, 6, DGN_REQUEST | dest,
         [dgn & 0xFF, (dgn >> 8) & 0xFF, (dgn >> 16) & 0xFF, instance, 0xFF, 0xFF, 0xFF, 0xFF])


def collect(bus, secs):
    """All extended data frames (no error frames) received within `secs`: [(timestamp, id, data)]."""
    out, t0 = [], time.time()
    while time.time() - t0 < secs:
        m = bus.recv(0.05)
        if m is not None and m.is_extended_id and not m.is_error_frame:
            out.append((m.timestamp, m.arbitration_id, bytes(m.data)))
    return out


def frames(msgs, dgn, sa=None):
    """[(timestamp, data)] for frames with this on-wire DGN (and source address, if given)."""
    return [(t, d) for t, i, d in msgs if parse(i)[1] == dgn and (sa is None or parse(i)[2] == sa)]


def angle(raw):
    return raw / 128.0 - 200.0


def encode_angle(deg):
    v = round((deg + 200) * 128)
    return [v & 0xFF, v >> 8]


def status_angles(data):
    """-> (pitch, roll, 'DFPR' level bits) from a LEVELING_SENSOR_STATUS payload."""
    b = data[0]
    bits = f"{b & 3}{b >> 2 & 3}{b >> 4 & 3}{b >> 6 & 3}"
    return angle(data[1] | data[2] << 8), angle(data[3] | data[4] << 8), bits


def find_sensor(bus, secs=2.5):
    """Source address of the node broadcasting LEVELING_SENSOR_STATUS, or None."""
    for t, i, d in collect(bus, secs):
        if parse(i)[1] == DGN_STATUS:
            return parse(i)[2]
    return None
