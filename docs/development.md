# Development

## Unit tests

```bash
pio test -e native                                                          # firmware logic
pio run -e esp32 && pio run -e esp32_level && pio run -e esp32_ac           # all three builds compile
uv run --with pytest --with jinja2 --with pyyaml pytest homeassistant/tests # HA package (YAML)
uv run --python 3.14 --with-requirements tests/requirements.txt pytest tests # HA integration
uv run --with pytest --with cryptography pytest tools/ota                   # update protocol and signing
uv run --with pytest --with cryptography pytest tools/ac                    # AC bridge HA wire format
```

`pio test -e native` covers the AC bridge too: the unit's wire protocol (`test_unit_protocol`),
the command queue and its timing (`test_bridge`), the unit link's back-off and bind rules
(`test_link_policy`), the instance-7 RV-C codec and panel behavior (`test_thermostat_codec`,
`test_ac_panel`), and the Home Assistant link's crypto and session handling (`test_ha_link`,
`test_ha_control`). `tools/ac/` holds a Python twin of the Home Assistant wire format and the
shared test vectors (`uv run tools/ac/vectors.py` prints them; the C++ and Python tests check
the same values). The integration's AC tests are `tests/test_ac_*.py`.

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

## AC bridge bench tools

`tools/ac/ha_client.py` does what the Home Assistant integration does, from a Linux computer with
Bluetooth, so the sensor's Home Assistant service can be tested without Home Assistant. Open the
pairing window with `pair` on the sensor's console first.

```bash
uv run tools/ac/ha_client.py pair                   # pair; saves the key in ~/.config/rv-level/ha_pair.json
uv run tools/ac/ha_client.py status --watch 30      # open a session, print status notifications
uv run tools/ac/ha_client.py cmd setpoint 72        # one command and its result
uv run tools/ac/ha_client.py cmd setpoint 72 --replay   # resend it: expect BadAuth
uv run tools/ac/ha_client.py stranger --hold 60     # no key: must not disturb a real session
```

Ops are `power`, `mode` (0 cool, 1 eco, 2 sleep, 3 turbo, 4 fan), `fan` (0 auto, 1–5),
`setpoint` (°F), `step` (+1/−1), `display`, `oscillate`, `light`, `celsius` (0/1) and
`clear_preset`. The wire format is in [protocol.md](protocol.md#home-assistant-control-service).

Sensor console (USB, 115200 baud): `pair`, `unpair`, `forget-ac`, plus `zero` and `reboot`. The
AC bridge prints a status line every second starting `AC link=`.

RV-C checks for instance 7, with the sensor at BF, standing in for the panel (address 9F) on the
bench:

| Check | Send | Expect |
|---|---|---|
| Status | none | `19FFE2BF`, `19FF9CBF`, `19FEFABF`, `18FFA1BF` on change and every 5 s while the AC answers; none when it does not |
| Cool | `19FEF99F#0731FFFFFFFFFFFF` | AC on and cooling; status follows |
| Setpoint up / down | `19FEF99F#07FFFFFFFFFAFFFF` / `…F9FFFF` | Setpoint ±1 °F |
| Eco preset | `19FEF89F#07FFC4FFFFFFFFFF` | `19FEFABF` byte 3 `C4`; send again to clear |
| Display toggle | `18FFA09F#07F7FFFFFFFFFFFF` | Display toggles; `18FFA1BF` byte 1 bit 2 follows |
| Outage | power off the AC | Instance-7 status stops; the panel goes stale after 30 s |

The sensor ignores instance numbers other than 7 and frames from its own address.

Still to check on the hardware before cut-over:

- **Panel fan and setpoint buttons.** Capture (`candump`) the real panel's fan and setpoint button
  frames and confirm the fan bits match what the sensor decodes.
- **Four links at once.** AC unit, Home Assistant and an extra central (for example
  `ha_client.py stranger --hold 60`) connected together; BTHome adverts still visible.
- **Unit power pulled.** Pull the AC's power: instance-7 status stops and Home Assistant's
  "AC responding" turns off within about 15 s, and the panel goes stale.

### Earlier pre-release builds

A device flashed with an earlier pre-release build of the AC bridge branch stored the AC's
address in the wrong byte order. After flashing a current build, type `forget-ac` and then `pair`
once on its console to bind the unit again.
