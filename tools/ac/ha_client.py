#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["bleak>=0.22", "cryptography>=42"]
# ///
"""Bench client for the AC bridge's Home Assistant service: does what the HA integration does.

  uv run tools/ac/ha_client.py pair                  # during the pairing window ('pair' on the console)
  uv run tools/ac/ha_client.py status [--watch 30]   # session, read status, print notifications
  uv run tools/ac/ha_client.py cmd power 1           # session, one command, print its result
  uv run tools/ac/ha_client.py cmd setpoint 72 --replay   # then resend it: expect BadAuth
  uv run tools/ac/ha_client.py stranger [--hold 60]  # no key: 02 + bad 03 + a command; must not lock HA out

Ops: power, mode (0 cool, 1 eco, 2 sleep, 3 turbo, 4 fan), fan (0 auto, 1-5), setpoint (°F),
step (+1/-1), display, oscillate, light, celsius (0/1), clear_preset (value ignored).

The pairing key lives in --key-file (default ~/.config/rv-level/ha_pair.json). Every connection
proves the key (02 challenge, 03 proof) and is a new session with a new nonce, so the counter
restarts at 1; the file records the last session's nonce and counter.
"""
import argparse
import asyncio
import json
import os
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

sys.path.insert(0, str(Path(__file__).parent))
import ha_proto as p  # noqa: E402
from vectors import raw_pub  # noqa: E402

DEFAULT_KEY_FILE = Path.home() / ".config" / "rv-level" / "ha_pair.json"


class ClientError(Exception):
    pass


def load_state(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        raise ClientError(f"no pairing key in {path}: run 'pair' first") from None


def save_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(state, f, indent=2)
    tmp.replace(path)


async def find(address, timeout):
    if address:
        dev = await BleakScanner.find_device_by_address(address, timeout=timeout)
    else:
        # The service UUID is in the scan response of AC builds.
        dev = await BleakScanner.find_device_by_filter(
            lambda _, ad: p.SERVICE in [u.lower() for u in ad.service_uuids], timeout=timeout)
    if dev is None:
        raise ClientError(f"no AC bridge found within {timeout} s")
    return dev


async def connect(address, attempts=5):
    for i in range(attempts):
        client = BleakClient(await find(address, 15), timeout=15)
        try:
            await client.connect()
            acquire = getattr(client._backend, "_acquire_mtu", None)  # BlueZ reports 23 until fetched
            if acquire is not None:
                await acquire()
            return client
        except Exception as e:  # noqa: BLE001  any failure here is a retry
            print(f"connect attempt {i + 1} failed ({type(e).__name__}: {e}), retrying")
    raise ClientError(f"could not connect in {attempts} attempts")


async def expect(q, timeout, what):
    try:
        return await asyncio.wait_for(q.get(), timeout)
    except asyncio.TimeoutError:
        raise ClientError(f"no {what} within {timeout} s") from None


async def auth(client, q, msg):
    await client.write_gatt_char(p.AUTH, msg, response=True)
    return await expect(q, 5, "auth reply")


async def start_session(client, key):
    """02 for a challenge, then 03 with the proof; returns the session nonce."""
    q = asyncio.Queue()
    await client.start_notify(p.AUTH, lambda _, d: q.put_nowait(bytes(d)))
    nonce = p.parse_session(await auth(client, q, p.session_msg()))
    p.parse_prove(await auth(client, q, p.prove_msg(key, nonce)))
    return nonce


async def cmd_pair(args):
    ha = X25519PrivateKey.generate()
    c = await connect(args.address)
    try:
        q = asyncio.Queue()
        await c.start_notify(p.AUTH, lambda _, d: q.put_nowait(bytes(d)))
        esp_pub, key = p.finish_pair(ha, await auth(c, q, p.pair_msg(raw_pub(ha))))
    finally:
        await c.disconnect()
    save_state(args.key_file, {"address": c.address, "esp_pub": esp_pub.hex(), "key": key.hex()})
    print(f"paired with {c.address}; key saved to {args.key_file}")


def show_status(d):
    s = p.parse_status(d)
    print(json.dumps(s) if s else "(empty: no authenticated session)")


async def cmd_status(args):
    state = load_state(args.key_file)
    c = await connect(args.address or state.get("address"))
    try:
        before = await c.read_gatt_char(p.STATUS)
        print(f"read before auth: {len(before)} bytes{' (expected 0)' if before else ''}")
        await start_session(c, bytes.fromhex(state["key"]))

        def on_status(_, d):
            print("notify:", end=" ")
            show_status(bytes(d))

        await c.start_notify(p.STATUS, on_status)
        print("read:", end=" ")
        show_status(bytes(await c.read_gatt_char(p.STATUS)))
        await asyncio.sleep(args.watch)
    finally:
        await c.disconnect()


async def cmd_cmd(args):
    state = load_state(args.key_file)
    key = bytes.fromhex(state["key"])
    op = p.op_code(args.op)
    c = await connect(args.address or state.get("address"))
    try:
        nonce = await start_session(c, key)
        counter = state["counter"] + 1 if state.get("nonce") == nonce.hex() else 1
        q = asyncio.Queue()
        await c.start_notify(p.COMMAND, lambda _, d: q.put_nowait(p.parse_result(bytes(d))))
        msg = p.command_msg(key, nonce, counter, op, args.value)
        save_state(args.key_file, {**state, "nonce": nonce.hex(), "counter": counter})
        sends = [("command", msg)] + ([("replay", msg)] if args.replay else [])
        if args.bad_mac:
            sends = [("bad mac", msg[:-1] + bytes([msg[-1] ^ 1]))]
        for what, m in sends:
            await c.write_gatt_char(p.COMMAND, m, response=True)
            got_counter, result = await expect(q, 12, f"result for the {what}")
            print(f"{what}: counter {got_counter} -> {result}")
            if got_counter != counter:
                raise ClientError(f"result for counter {got_counter}, sent {counter}")
    finally:
        await c.disconnect()


async def cmd_stranger(args):
    """What anyone in range can do without the key. HA's session must survive all of it."""
    c = await connect(args.address)
    try:
        q = asyncio.Queue()
        await c.start_notify(p.AUTH, lambda _, d: q.put_nowait(bytes(d)))
        nonce = p.parse_session(await auth(c, q, p.session_msg()))
        print(f"challenge: {nonce.hex()}")
        reply = await auth(c, q, p.prove_msg(os.urandom(32), nonce))
        print(f"bad proof -> {reply.hex()} (expected 0300)")
        rq = asyncio.Queue()
        await c.start_notify(p.COMMAND, lambda _, d: rq.put_nowait(p.parse_result(bytes(d))))
        await c.write_gatt_char(p.COMMAND, p.command_msg(os.urandom(32), nonce, 1, p.OPS["power"], 0), response=True)
        print(f"forged command -> {await expect(rq, 5, 'result')} (expected BadAuth)")
        print(f"status read: {len(await c.read_gatt_char(p.STATUS))} bytes (expected 0)")
        print(f"holding the link {args.hold} s (the board drops unclaimed links after 30 s)")
        for _ in range(int(args.hold)):
            if not c.is_connected:
                print("dropped by the board")
                break
            await asyncio.sleep(1)
    finally:
        await c.disconnect()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--key-file", type=Path, default=DEFAULT_KEY_FILE)
    ap.add_argument("--address", help="Bluetooth address (default: the paired one, else scan by service)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pair", help="pair while the pairing window is open")
    s = sub.add_parser("status", help="start a session, read status, print notifications")
    s.add_argument("--watch", type=float, default=0, help="seconds to keep printing notifications")
    c = sub.add_parser("cmd", help="send one command")
    c.add_argument("op", help=", ".join(p.OPS))
    c.add_argument("value", type=int)
    c.add_argument("--replay", action="store_true", help="resend the same message: expect BadAuth")
    c.add_argument("--bad-mac", action="store_true", help="send it with a corrupted MAC: expect BadAuth")
    st = sub.add_parser("stranger", help="act as an unpaired device: must not disturb HA's session")
    st.add_argument("--hold", type=float, default=40, help="seconds to keep the link")
    args = ap.parse_args()
    try:
        asyncio.run({"pair": cmd_pair, "status": cmd_status, "cmd": cmd_cmd, "stranger": cmd_stranger}[args.cmd](args))
    except (ClientError, p.ProtoError, ValueError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
