# Development

## Unit tests

```bash
pio test -e native                                                          # firmware logic
uv run --with pytest --with jinja2 --with pyyaml pytest homeassistant/tests # HA package (YAML)
uv run --python 3.14 --with-requirements tests/requirements.txt pytest tests # HA integration
uv run --with pytest --with cryptography pytest tools/ota                   # update protocol and signing
```

The integration tests need uv's Python 3.14.2 or newer, because Home Assistant 2026.10
requires it. A bare `pytest` from the repository root is not configured; use the commands
above (the integration's config is `tests/pytest.ini`).

## Hardware tests

`tools/hwtest/` exercises a flashed sensor through a SocketCAN adapter (`can0`, 250 kbit/s) and
its USB serial port. Each script declares its own dependencies, so `uv run` needs no setup. They
use `80` as the tool's address.

| Script | Checks | Changes saved settings? |
|---|---|---|
| `bus_test.py` | Status/DM_RV timing and encoding, address claim and conflicts, PRODUCT_ID, request routing and NAKs, threshold commands, instance filtering, set-zero ACK | **Yes**: thresholds, zero, and moves the address to BEh |
| `wheel_test.py` | LEVEL_WHEEL_STATUS records and rate, config commands, signature filtering, range rejection, requests | Restores the configuration it found |
| `boot_test.py` | Resets over USB; claim and first-status timing; prints both config statuses | No |
| `tilt_test.py` | Guided tilts to confirm the axis map | No |
| `motion_zero_test.py` | Set-zero while moving gets NAK 3 | No |
| `serial_log.py` | Captures serial output, optionally from reset | No |
| `ble_test.py` | BTHome name, payload, packet ID, values against serial, advert rate | No |

```bash
uv run tools/hwtest/bus_test.py                       # keep the sensor still
uv run tools/hwtest/boot_test.py --port /dev/ttyUSB0
```

After `bus_test.py`, restore defaults: `pio run -e esp32 -t erase && pio run -e esp32 -t upload`.

## Bench checks with can-utils

```bash
sudo ip link set can0 up type can bitrate 250000
candump -ta can0
```

Frames below are sent from address 80h to the sensor at BFh.

| Check | Send | Expect |
|---|---|---|
| Start-up | power on | `18EABFFE`, then `18EE00BF`, then `0DFFEBBF` every second and four `19EFBFBF` records every second |
| PRODUCT_ID | `18EABF80#EBFE00FFFFFFFFFF` | `18ECFFBF`, then five `18EBFFBF` 50 ms apart |
| DM_RV | `18EABF80#CAFE01FFFFFFFFFF` | `19FECABF` `05 51 FF FF FF FF FF FF` |
| Zero pitch | `19FDE880#01FFFFFFFFFDFFFF` | ACK `18E880BF` `00 01 FF FF 80 E8 FD 01`, then `19FDE9BF` |
| Pitch thresholds 2.0° / 1.0° | `19FDE880#0100658064FFFFFF` | `19FDE9BF` `01 00 65 80 64 FC FF FF`, no ACK |
| Threshold out of range | `19FDE880#01008CFFFFFFFFFF` | NAK `18E880BF` code `05` |
| Block config | `19EFBF80#4C5602140C19FFFF` | `19EFBFBF` `04 36 06 AD 02 14 0C 19` |
| Block config request | `18EABF80#BFEF01FFFFFFFFFF` | Records 01h–04h |
| Reboot | `197FBF80#FDFFFFFFFFFFFFFF` | ACK `00 01 FF FF 80 00 7F 01`, restart, address claim |
| Factory defaults | `197FBF80#DFFFFFFFFFFFFFFF` | ACK `00`, record 04h and both config statuses with defaults |
| MPU-6050 fault | unplug the GY-521 | LED red, `19FECABF` `15 51 00 00 4C FF FF FF` every second, status angles `FE FF` |

Use `cansend can0 <frame>` for each. Pitch bytes 1–2 of `0DFFEBBF` increase when the front is
raised; roll bytes 3–4 increase when the driver side is raised.
