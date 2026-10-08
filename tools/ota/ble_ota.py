#!/usr/bin/env python3
"""Update the level sensor's firmware over Bluetooth. Runs on the Pi, which has the CAN bus and
Bluetooth; tools/ota_deploy.sh copies it there with common.py and runs it.

  python ble_ota.py firmware.bin [--bus can1] [--sa N] [--corrupt-byte N]
  python ble_ota.py --info           # open the window, print Info, close it
  python ble_ota.py --open-window    # just open the window (for testing that it closes)
  python ble_ota.py --connect-test   # try to connect without opening the window
"""
import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

import common
import ota_proto as p

NAME = "RV Level"


class OtaError(Exception):
    pass


def open_window(bus, sa):
    """GENERAL_RESET Reboot/Enter Bootloader to `sa`; returns once it ACKs 0."""
    common.send(bus, 6, common.DGN_GENERAL_RESET | sa, p.general_reset_enter_bootloader())
    t0 = time.time()
    while time.time() - t0 < 2:
        m = bus.recv(0.1)
        if m is None or not m.is_extended_id:
            continue
        _, dgn, src = common.parse(m.arbitration_id)
        if dgn == (common.DGN_ACK | common.TOOL_SA) and src == sa and bytes(m.data[5:8]) == b"\x00\x7f\x01":
            if m.data[0] != 0:
                raise OtaError(f"GENERAL_RESET refused: ACK code {m.data[0]}")
            return
    raise OtaError("no ACK to GENERAL_RESET within 2 s")


def wait_for_status(bus, sa, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        m = bus.recv(0.2)
        if m is not None and m.is_extended_id and common.parse(m.arbitration_id)[1:] == (common.DGN_STATUS, sa):
            return
    raise OtaError(f"sensor not back on the bus within {secs} s")


async def find(timeout=15):
    dev = await BleakScanner.find_device_by_name(NAME, timeout=timeout)
    if dev is None:
        raise OtaError(f'"{NAME}" not found over Bluetooth within {timeout} s')
    return dev


async def connect(attempts=10):
    """Connected BleakClient. Many new connections fail with 0x3E ("connection failed to be
    established"): the link forms but the sensor never answers on it, so retry."""
    for i in range(attempts):
        client = BleakClient(await find(), timeout=15)
        try:
            await client.connect()
            return client
        except Exception as e:  # noqa: BLE001  any failure here is a retry
            print(f"connect attempt {i + 1} failed ({type(e).__name__}), retrying")
    raise OtaError(f"could not connect in {attempts} attempts")


async def acquire_mtu(client):
    # BlueZ reports 23 until the MTU is fetched explicitly.
    acquire = getattr(client._backend, "_acquire_mtu", None)
    if acquire is not None:
        await acquire()


async def read_info(client):
    return p.parse_info(await client.read_gatt_char(p.INFO))


async def control(client, q, payload, timeout=10):
    await client.write_gatt_char(p.CONTROL, payload, response=True)
    return await expect(q, timeout)


async def expect(q, timeout):
    try:
        return await asyncio.wait_for(q.get(), timeout)
    except asyncio.TimeoutError:
        raise OtaError(f"no reply from the sensor within {timeout} s") from None


def fail(name, value):
    hint = p.flash_hint(value) if name == "ERR_FLASH" else ""
    raise OtaError(f"{name} (0x{value:X}){': ' + hint if hint else ''}")


async def upload(image, corrupt_byte):
    sent = bytearray(image)
    if corrupt_byte is not None:
        sent[corrupt_byte] ^= 0xFF  # BEGIN still carries the true hash: FINISH must refuse
    c = await connect()
    try:
        await acquire_mtu(c)
        old = await read_info(c)
        print(f"sensor: {old['version']} ({old['state']}, slot {old['slot']})")
        q = asyncio.Queue()
        await c.start_notify(p.CONTROL, lambda _, d: q.put_nowait(p.parse_notice(d)))
        name, value = await control(c, q, p.begin_cmd(image), timeout=25)
        if name != "OK":
            fail(name, value)
        s = p.Sender(len(image), p.chunk_size(c.mtu_size))
        t0, last = time.time(), -1
        while not s.done:
            w = s.next_write()
            if w is not None:
                off, n = w
                await c.write_gatt_char(p.DATA, p.data_write(off, bytes(sent[off:off + n])), response=False)
                if q.empty():
                    continue
            name, value = await expect(q, 15)
            if name == "PROGRESS":
                s.on_progress(value)
                pct = value * 100 // len(image)
                if pct != last:
                    print(f"\r{pct:3d}%  {value / max(time.time() - t0, 0.001) / 1024:.1f} KB/s", end="", flush=True)
                    last = pct
            elif name == "RESEND":
                s.on_resend(value)
            else:
                fail(name, value)
        print()
        name, value = await control(c, q, bytes([p.FINISH]), timeout=30)
        if name != "DONE":
            fail(name, value)
        print(f"sent {len(image)} bytes in {time.time() - t0:.0f} s; sensor rebooting")
        return old["version"]
    finally:
        await c.disconnect()


async def info_and_close():
    c = await connect()
    try:
        info = await read_info(c)
        try:
            await c.write_gatt_char(p.CONTROL, bytes([p.CLOSE]), response=True)
        except Exception:  # noqa: BLE001
            pass  # the sensor drops the link as soon as the window closes, racing the write's reply
        return info
    finally:
        await c.disconnect()


async def connect_test():
    """True if the sensor accepts a connection (it only does while the update window is open)."""
    try:
        c = await connect(attempts=5)
    except OtaError:
        return False
    await c.disconnect()
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", nargs="?")
    ap.add_argument("--bus", default="can1")
    ap.add_argument("--sa", type=lambda v: int(v, 0))
    ap.add_argument("--corrupt-byte", type=int)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--info", action="store_true")
    mode.add_argument("--open-window", action="store_true")
    mode.add_argument("--connect-test", action="store_true")
    a = ap.parse_args()

    if a.connect_test:
        ok = asyncio.run(connect_test())
        print("connected" if ok else "could not connect")
        return 0

    bus = common.open_bus(a.bus)
    try:
        sa = a.sa if a.sa is not None else common.find_sensor(bus, 3)
        if sa is None:
            raise OtaError(f"no LEVELING_SENSOR_STATUS on {a.bus} within 3 s; is the sensor on this bus?")
        open_window(bus, sa)
        print(f"update window open on sensor 0x{sa:02X}")
        if a.open_window:
            return 0
        if a.info:
            print(asyncio.run(info_and_close()))
            return 0
        if not a.firmware:
            ap.error("firmware.bin is required")
        with open(a.firmware, "rb") as f:
            image = f.read()
        old = asyncio.run(upload(image, a.corrupt_byte))
        time.sleep(2)
        wait_for_status(bus, sa, 30)
        print("sensor back on the bus; waiting 35 s for it to confirm the new firmware")
        time.sleep(35)
        open_window(bus, sa)
        info = asyncio.run(info_and_close())
        print(f"sensor: {info['version']} ({info['state']}, slot {info['slot']})")
        if info["version"] == old:
            raise OtaError("version unchanged: the new firmware rolled back")
        if info["state"] != "confirmed":
            raise OtaError("new firmware still on trial")
        print("update OK")
        return 0
    except OtaError as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        return 1
    finally:
        bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
