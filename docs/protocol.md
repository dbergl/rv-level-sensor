# Protocol reference

All multi-byte integers are little endian. "n/a" fields are all ones (`FF` / `FFFF`).

## RV-C

250 kbit/s, 29-bit IDs: `priority << 26 | DGN << 8 | source address`. The examples below assume
the sensor has address BFh. Leveling messages are present in builds with leveling; the
instance-7 air conditioner is present in builds with the AC bridge (see
[setup.md §7](setup.md#7-build-options)).

### Address and identity

- Claims an address from BFh (191) down to B0h (176), starting at the last address it held.
  At start-up it sends a request for ADDRESS_CLAIM (`18EABFFE`), then ADDRESS_CLAIM
  (`18EE00BF`) 250 ms later, and starts normal traffic.
- NAME: arbitrary-address capable, manufacturer code 0, identity number from the low 21 bits of
  the MAC address.
- PRODUCT_ID (0FEEBh): `DIY*LEVEL-MPU6050*<MAC>*` (`LEVEL-AC` with the AC bridge too,
  `AC-COUNTRYMOD` with the AC bridge alone), sent as a multi-packet (TP.CM BAM then
  TP.DT every 50 ms) on a directed request.
- Instance 1, DSA 81 (51h).

### Messages sent

| CAN ID | DGN | When |
|---|---|---|
| `18EE00BF` | ADDRESS_CLAIM | Start-up, on request, on conflict |
| `0DFFEBBF` | LEVELING_SENSOR_STATUS 1FFEBh | Every 1 s; on change (≥ 100 ms apart) |
| `19FDE9BF` / `19FDEBBF` | Pitch / roll config status | On request; after an applied config command |
| `19FECABF` | DM_RV 1FECAh | Every 5 s (1 s while faulted); on fault change; on directed request |
| `19EFBFBF` | LEVEL_WHEEL_STATUS 1EFBFh | Records 01h–04h every 1 s; 01h–03h on change (≥ 250 ms apart) |
| `18E8xxBF` | ACK 0E800h | Replies, to the requester `xx` |
| `19FFE2BF`, `19FF9CBF`, `19FEFABF`, `18FFA1BF` | Instance-7 air conditioner status | AC bridge builds: on change, at least every 5 s ([below](#air-conditioner-instance-7-ac-bridge-builds)) |

### LEVELING_SENSOR_STATUS (1FFEBh, priority 3)

| Byte | Field |
|---|---|
| 0 | Bits 0–1 driver side low, 2–3 front low, 4–5 passenger side low, 6–7 rear low (`01` low, `00` not). `FF` when faulted |
| 1–2 | Pitch, `(deg + 200) × 128`. + nose up. `FFFE` when faulted |
| 3–4 | Roll, same encoding. + driver side up. `FFFE` when faulted |
| 5–6 | n/a |
| 7 | Instance |

A side is "low" once its angle passes the out-of-level threshold, and stays low until it is back
within the within-level threshold. "On change" means a change in the bits or validity, or an
angle move over 0.05°.

### Pitch / roll configuration

Command 1FDE8h (pitch) / 1FDEAh (roll); status 1FDE9h / 1FDEBh. Same layout:

| Byte | Field |
|---|---|
| 0 | Instance. Commands accept 0 (all) or 1 |
| 1–2 | Out-of-level threshold, angle encoding. `FFFF` = no change |
| 3–4 | Within-level threshold. `FFFF` = no change |
| 5 | Bits 0–1: `00` no action, `01` set zero here, `11` n/a. Status always sends `00` |
| 6–7 | n/a |

Thresholds must satisfy 0 ≤ within ≤ out ≤ 45°. Defaults: out 1.0°, within 0.5°, zero 0°.
Set-zero stores the current filtered angle as that axis's zero.

ACK/NAK (0E800h) replies:

| Code | Sent when |
|---|---|
| `00` | Set-zero applied. Threshold-only changes are not ACKed |
| `03` | Set-zero while the sensor is moving or has no valid reading |
| `04` | Bad format (byte 5 = `10`, or under 6 bytes) |
| `05` | Threshold out of range. Nothing changes |

ACK layout: byte 0 code, 1 instance, 2–3 `FF`, 4 the commander's address, 5–7 the acknowledged
DGN.

### DM_RV (1FECAh)

| Byte | OK | Faulted (MPU-6050 not answering) |
|---|---|---|
| 0 | `05` (on, active) | `15` (adds yellow lamp) |
| 1 | DSA `51` | DSA `51` |
| 2–4 | `FF FF FF` | SPN 2, FMI 12: `00 00 4C` |
| 5–7 | n/a | n/a |

### Requests (0EA00h)

| Requested DGN | Reply |
|---|---|
| ADDRESS_CLAIM | ADDRESS_CLAIM |
| PRODUCT_ID, DM_RV | Directed requests only |
| 1FFEBh, 1FDE9h, 1FDEBh | The status (instance FFh, 1, or 0 for the config statuses) |
| 1EFBFh or 1EF00h | LEVEL_WHEEL_STATUS records 01h–04h |
| Anything else | NAK `01` if directed; ignored if global |

### GENERAL_RESET (17F00h)

Accepted only when directed to the sensor's address (`197FBFxx`). Each field is 2 bits; `01`
selects it.

| Field | Action |
|---|---|
| Byte 0 bits 0–1, Reboot | ACK `00`, restart 250 ms later |
| Byte 0 bits 4–5, Restore defaults | ACK `00`. Resets thresholds, zero and block config; sends both config statuses and record 04h |
| Byte 1 bits 4–5, Reboot/Enter bootloader | ACK `00`, opens the Bluetooth update window (restarts instead if Bluetooth is not running or updates are disabled in the build) |
| Byte 0 bits 6–7, byte 1 bits 0–1, 2–3, 6–7 | NAK `01`; nothing changes |
| Any field `10` | NAK `04` |

### LEVEL_WHEEL_STATUS (1EFBFh, proprietary)

DGN 1EF00h with destination BFh, always, whatever address the sensor holds. Byte 0 is the
record number. Wheel order is FL, FR, RL, RR (FL = driver side front).

**Record 01h, attitude**

| Byte | Field |
|---|---|
| 1–2 | \|pitch\|, 0.1° |
| 3–4 | \|roll\|, 0.1° |
| 5 | Bits 0–1 level, 2–3 short on blocks, 4–5 sensor fault, 6–7 settling (sensor moving) |
| 6 | Bits 0–1 nose up, 2–3 nose down, 4–5 driver side up, 6–7 driver side down |
| 7 | Total layers |

**Record 02h, layers**: bytes 1–4 block layers per wheel; byte 5 bits 0–1 tolerance `01` auto
or `00` fixed, other bits 1; bytes 6–7 n/a.

**Record 03h, lift**: bytes 1–4 lift per wheel, 0.1 in; bytes 5–7 n/a.

**Record 04h, configuration**

| Byte | Field | Default |
|---|---|---|
| 1–2 | Wheelbase, 0.1 in | 1590 (159.0 in) |
| 3–4 | Track, 0.1 in | 685 (68.5 in) |
| 5 | Block height, 0.05 in | 20 (1.00 in) |
| 6 | Blocks carried | 10 |
| 7 | Tolerance in force, 0.01 in (auto shows half the block height) | auto (50) |

Counts and lifts above FAh are sent as `FE`. While faulted, records 01h–03h carry n/a values
and record 01h byte 5 is `DF` (fault `01`, other fields n/a).

The displayed angles, and the block plan made from them, change only when the reading moves
over 0.05°.

**Block plan.** With `a = wheelbase/2 · tan(pitch)` and `b = track/2 · tan(roll)`, the wheel
heights are FL `a+b`, FR `a−b`, RL `b−a`, RR `−a−b`. Each wheel's lift is the highest wheel's
height minus its own. A lift at or below the tolerance gets 0 layers; otherwise the nearest
whole number of blocks, exactly half rounding down. Level = every wheel 0 layers. Short on
blocks = total layers > blocks carried.

### LEVEL_CONFIG_COMMAND (1EF00h, proprietary)

DGN 1EF00h with destination BFh, the sensor's address, or FFh (`19EFBFxx` at the default
address). Bytes 0–1 are the signature `4C 56` ("LV"); frames without it are ignored. Byte 2 is
the opcode. Frames must be 8 bytes.

| Op | Payload (bytes 3–7) | Effect |
|---|---|---|
| 01h | wheelbase u16, track u16, `FF` | Set geometry |
| 02h | block height, carried, tolerance, `FF FF` | Set blocks. Tolerance `FE` = auto |
| 03h / 04h | n/a | Blocks carried +1 / −1 |
| 05h | n/a | Restore block defaults |
| 06h / 07h | n/a | Block height +0.25 / −0.25 in |
| 08h / 09h | n/a | Tolerance +0.05 / −0.05 in from the value in force (becomes fixed) |
| 0Ah | n/a | Tolerance auto |
| 0Bh / 0Ch | n/a | Wheelbase +0.5 / −0.5 in |
| 0Dh / 0Eh | n/a | Track +0.5 / −0.5 in |

All-ones fields leave a value unchanged. Steppers stop at the range ends.

| Setting | Range |
|---|---|
| Wheelbase | 80–300 in |
| Track | 40–110 in |
| Block height | 0.05–12.50 in (steppers 0.25–12.50 in) |
| Blocks carried | 0–250 |
| Tolerance | 0–2.00 in, or auto |

Every LV command is answered with record 04h. An out-of-range value or unknown opcode changes
nothing, and record 04h shows the unchanged settings. An applied command is also followed by
records 01h–03h. Settings are saved in flash.

### Air conditioner, instance 7 (AC bridge builds)

Builds with the AC bridge stand in for RV-C air conditioner/thermostat **instance 7**, which a
Firefly panel (v66) shows and controls. Byte 0 of every message below is the instance, `07`.
Status goes out at priority 6 from whatever address the sensor holds (`19FFE2BF`, `19FFE1BF`,
`19FF9CBF`, `19FEFABF`, `18FFA1BF` at BFh). The panel matches only the DGN and instance, not the sender.

**Sent** (all together, on a change and at least every 5 s):

| DGN | Name | Content |
|---|---|---|
| 1FFE2h | THERMOSTAT_STATUS_1 | Byte 1 bits 0–3 mode: `0` off, `1` cool, `4` fan only, `6` dehumidify, `F` unknown. Bits 4–5 fan mode: `00` auto, `01` fixed speed, `11` n/a. Bits 6–7 `11`. Byte 2 fan speed, 0.5 % per bit: level N is `N × 28h`; `C8` (100 %) on auto; `00` while off; `FF` if unknown. Bytes 3–4 `FF FF` (heat setpoint). Bytes 5–6 cool setpoint, `FFFF` if unknown. Byte 7 `FF` |
| 1FFE1h | AIR_CONDITIONER_STATUS | Byte 1 `01` (manual). Bytes 2–3 `C8 C8` (max fan / max output 100 %). Byte 4 fan speed, as 1FFE2 byte 2. Byte 5 compressor output: `C8` while on in cool or a preset, `00` in fan only or off, `FF` if the mode is unknown. Byte 6 `0A` (dead band 1.0 °C). Byte 7 `FF` |
| 1FF9Ch | THERMOSTAT_AMBIENT_STATUS | Bytes 1–2 air temperature; bytes 3–7 `FF`. Not sent while the temperature is unknown |
| 1FEFAh | THERMOSTAT_STATUS_2 | Byte 3: `C0` plus `01` sleep, `04` eco or `10` turbo while on; `FF` while off. Other bytes `FF` |
| 0FFA1h | Proprietary option status | Byte 1: `C0` plus `01` if the vent is oscillating, `04` if the display is on and `10` for Recirc. Bytes 2–3 the unit's low-voltage protection, u16 LE, 0.1 V per bit (shown on the panel's A/C SETUP page). Other bytes `FF` |

While the AC is off, mode is `0`, fan mode `11` and fan speed `00`, so the panel lights no mode,
Auto or speed bar, 1FEFA lights no preset, and 0FFA1 lights only Recirc. The setpoint and air temperature are still sent. A
mode the bridge cannot decode is sent as `F` (n/a), never as a guess.

**Silence is the stale signal.** The sensor sends none of these while it has no live AC unit: the
link is down, or no status frame has arrived for 10 s. The panel marks the AC stale after 30 s
without them. It also sends nothing until it has claimed an RV-C address.

**Temperature** is a u16 in 1/32 °C steps from −273 °C, `FFFF` n/a. The panel shows °F as
`(raw − 8736) × 9 / 160 + 32` with the fraction dropped, so the sensor rounds up to the next raw
value: 72 °F is 24E8h (`E8 24`).

**Received** (any sender other than the sensor itself, 8 bytes, byte 0 = `07`; the panel sends
from 9Fh at priority 6, `19FEF99F`, `19FEF89F`, `18FFA09F`; 1FFE0h is also accepted). Anything the unit cannot do is
ignored.

| DGN | Name | Effect |
|---|---|---|
| 1FEF9h | THERMOSTAT_COMMAND_1 | Byte 1 bits 0–3 mode: `0` power off, `1` cool (powers on if needed), `4` fan only; heat (`2`), dehumidify (`6`) and `F` do nothing. Bits 4–5 fan mode: `00` auto; `01` with byte 2 = 1–200 (0.5 % per bit) sets level `(raw + 20) / 40`, kept to 1–5. Bytes 5–6 cool setpoint: `FFFA` one °F up, `FFF9` one °F down, below `FE00` an absolute setpoint (ignored outside 61–86 °F). Heat setpoint (bytes 3–4) is ignored |
| 1FEF8h | THERMOSTAT_COMMAND_2 | Byte 2 low 6 bits: `01` sleep, `04` eco, `10` turbo, `00` back to plain cool, `3F` nothing. Pressing the preset that is already on clears it |
| 0FFA0h | Proprietary option command | Byte 1 bits 0–1 vent (`FD`), bits 2–3 display (`F7`), bits 4–5 Recirc (`DF`): `01` toggles, `00` off, `11` no change. Vent and display toggle from the unit's reported state. The unit has no fresh-air damper, so Recirc is held by the sensor only: it is not sent to the unit, and it goes back to on whenever the AC is off |
| 1FFE0h | AIR_CONDITIONER_COMMAND | Byte 5 compressor output, 0.5 % per bit: above 0 cool (powers on if needed); `00` fan only if byte 4 is above 0, else power off. Byte 4 fan speed above 0 sets level `(raw + 20) / 40`, kept to 1–5. Above `C8` is no change |

A panel command is done only when the unit's next status shows it; the panel then follows the
status, not the button. Commands are never held over an outage.

## BTHome (Bluetooth LE)

Name `RV Level`, service data UUID FCD2h, BTHome v2, unencrypted, regular interval
(device info `40`). Builds without leveling still advertise the
name but carry no BTHome data.

| Object ID | Value |
|---|---|
| `00` | Packet ID; changes when the content changes |
| `0F` | Generic boolean: 1 = no side below level (angle thresholds) |
| `22` | Moving |
| `26` | Problem: MPU-6050 fault |
| `3F` | Rotation: pitch, 0.1°, signed |
| `3F` | Rotation: roll, 0.1°, signed |

The two rotation objects are left out while the reading is invalid. The advert is refreshed
every 500 ms and sent every 250 ms. Unchanged data is re-sent with a new packet ID every 10 s.

## Bluetooth update service

Present only in builds with `BLE_OTA_DISABLED = false` (the default is `true`).

The sensor accepts connections only while the update window is open. A GENERAL_RESET
Reboot/Enter bootloader opens it for 5 minutes, and each control or data write moves the
deadline to 5 minutes later. While it is open the advert is connectable and the CPU runs at
240 MHz instead of 80 MHz (AC bridge builds always run at 240 MHz). RV-C and BTHome reporting continue during a transfer.

UUIDs share the base `af48xxxx-29e2-47f0-a341-4f1a49271bf4`:

| UUID | Characteristic | Properties |
|---|---|---|
| `af480001` | Service | |
| `af480002` | Info | read |
| `af480003` | Control | write, notify |
| `af480004` | Data | write without response |

**Info**: byte 0 image state (0 confirmed, 1 on trial), byte 1 running slot (0/1), bytes 2–3
seconds left in the window, bytes 4– firmware version (ASCII).

**Control write**, byte 0 opcode:

| Op | Payload | Effect |
|---|---|---|
| 01h BEGIN | size u32, SHA-256 (32 bytes), Ed25519 signature (64 bytes) | Check the signature, then start a transfer to the spare slot. Restarts one in progress |
| 02h FINISH | | Check size and SHA-256, validate, set boot slot, notify DONE, restart after 500 ms |
| 03h ABORT | | Abort the transfer; the window stays open |
| 04h CLOSE | | Close the window |

**Control notify**, 5 bytes: code u8, value u32.

| Code | Name | Value |
|---|---|---|
| 00h | OK | Reply to BEGIN, ABORT, CLOSE |
| 01h | PROGRESS | Bytes written; every 4096 bytes |
| 02h | RESEND | Next offset expected |
| 03h | DONE | Image size |
| 10h | ERR_WINDOW_CLOSED | 0 |
| 11h | ERR_BAD_SIZE | Largest size accepted |
| 12h | ERR_NOT_STARTED | 0 |
| 13h | ERR_INCOMPLETE | Bytes received |
| 14h | ERR_HASH | 0; transfer aborted |
| 15h | ERR_INVALID_IMAGE | `esp_err_t` |
| 16h | ERR_FLASH | `esp_err_t`; transfer aborted |
| 17h | ERR_BAD_COMMAND | Opcode, or 0 for a bad length |
| 18h | ERR_SIGNATURE | 0: BEGIN without a signature; 1: signature does not verify |

**Signature.** Ed25519 over 52 bytes: `RV-Level OTA v1` and a NUL (16 bytes), the image size
(u32), and the image's SHA-256. The public key is built into the firmware. The signature is
checked before anything is erased, and a refused BEGIN leaves any transfer in progress
untouched. FINISH then checks that the received data has the signed SHA-256.

**Data write**: offset u32, then image bytes; the whole write must fit in 512 bytes. A write at
the wrong offset is dropped and answered with one RESEND per gap. Data past `size` gives
ERR_BAD_SIZE and aborts. A disconnect aborts the transfer. The uploader keeps at most 16 KB in
flight beyond the last PROGRESS.

**Trial boot.** A new image starts on trial. It confirms itself once it has run for 30 s, claimed an RV-C
address and read the MPU-6050. Otherwise it rolls back to the previous image at 60 s.

## AC unit link (Bluetooth LE)

Builds with the AC bridge are the Bluetooth central for a CountryModPro rooftop AC. The unit
accepts one client at a time, so while the sensor is connected the vendor app and any other
controller cannot connect.

- **Finding it.** The unit advertises a local name starting `KT`. The sensor binds to one unit
  only, during a pairing window ([usage.md §10](usage.md#10-air-conditioner-ac-bridge)), by
  scanning for 10 s and taking the strongest `KT` name. The address is stored and no other unit
  is ever connected. A candidate is saved only after it connects, has the service below and
  sends a valid status frame within 5 s; otherwise it is logged and dropped.
- **Service** `0000ffe0-0000-1000-8000-00805f9b34fb`. Status arrives as notifications on
  `0000ffe1-…`; commands are written, with response, to `0000ffe2-…`.
- **Reconnecting.** After a drop the sensor retries after 1 s, doubling to a 30 s maximum.
- **Pacing.** At least 400 ms between writes. A command whose next status does not show it
  done, or that gets no reply within 3 s, is sent once more; after that it fails. The unit is queried every 3 s
  when idle.
- **Responding** means the link is up and a valid status frame arrived within the last 10 s.

**Command frame** (9 bytes): `5A 5A 06 01 func data checksum 0D 0A`. The checksum is the low
byte of the sum of the first six bytes. Only these are ever sent:

| func | data | Meaning |
|---|---|---|
| 01 | 00 / 01 / 02 | Status query / power off / power on |
| 02 | 01 / 02 / 04 / 05 / 06 | Mode cool / fan only / eco / sleep / turbo |
| 03 | 61–86 | Setpoint, °F, only while the unit shows °F |
| 04 | 01–05 / 06 | Fan level / auto |
| 0A | 01 / 02 | Display off / on |
| 45 | 00 / 01 | Vent static / oscillating |
| 1C | 01 / 02 | Light on / off |
| 20 | 01 / 02 | The unit's own scale °C / °F |

**Status frame** (21 bytes, from every query and command; it can arrive split over several
notifications, and the sensor resynchronizes on `5A 5A`). Byte 18 is the low byte of the sum of
bytes 0–17; bytes 19–20 are `0D 0A`.

| Field | Where |
|---|---|
| Power | byte 2 bit 7 |
| Display | byte 2 bit 0 |
| Fan level | `(byte 2 >> 1) & 7`: 0 auto, 1–5 |
| Mode | If byte 3 & `B0` is `10` sleep, `20` eco, `80` turbo, `00` none, else unknown. With none: byte 2 bits 4–5 both set is fan only, `20` alone is dehumidify, otherwise cool |
| Light | byte 3 bit 1 |
| Vent oscillating | byte 3 bit 3 |
| Air temperature | byte 4, in the unit's scale |
| Battery voltage | byte 8, whole volts |
| Low-voltage protection | `(256 + byte 9) / 10` V |
| Scale | byte 16: 1 °F, 0 °C |
| Setpoint | byte 17, in the unit's scale |

Byte 3 bit 6 is set while the vendor app is connected and is ignored. Air temperature outside
30–130 °F, setpoint outside 50–99 °F and battery outside 20–70 V count as unknown. The unit
changes the fan level itself on some mode changes, so the level always comes from the next
status. The bridge refuses setpoint changes while the unit shows °C.

## Home Assistant control service

The service is present in AC bridge builds, on the sensor's shared Bluetooth server. Its UUID is
also in the advert's scan response, which is how Home Assistant finds the sensor.

UUIDs share the base `af48xxxx-29e2-47f0-a341-4f1a49271bf4` with the update service:

| UUID | Characteristic | Properties |
|---|---|---|
| `af480101` | Service | |
| `af480102` | Auth | write, notify |
| `af480103` | Status | read, notify |
| `af480104` | Command | write, notify |

The sensor is connectable only when someone can use it: a paired Home Assistant with no live
session, or anyone while the pairing window is open (even beside a live session, so a second Home
Assistant can pair). A link that has not proved the pairing key is
dropped after 30 s. Notifications need an MTU of at least 37 (the 34-byte status plus 3); the
pairing reply (33 bytes) needs 36. Auth and command writes are queued, at most 2 per link, so a
stranger cannot crowd out Home Assistant.

### Pairing

The pairing window is opened at the board ([usage.md §10](usage.md#10-air-conditioner-ac-bridge)).
Subscribe to Auth notifications, then write `01 ‖ haPub` (haPub: 32-byte X25519 public key). The
reply is a notification `01 ‖ espPub`. `01` alone is a refusal: window closed, wrong length, MTU
too small, or a low-order key. A link not subscribed to Auth is refused too, and nothing is stored
(it could not receive the reply).

- The board makes a fresh X25519 key for each pairing and wipes it afterwards.
- `shared` = X25519(own private key, peer public key). An all-zero `shared` is refused.
- **Pair key** = BLAKE2b with a 32-byte digest, keyed with `shared` (32 bytes), over the ASCII
  label `RVL-AC-PAIR-v1` (no terminator) ‖ haPub ‖ espPub.
- The board stores the key in flash, closes the window and ends any session; the previous
  paired link is disconnected. One Home Assistant is paired at a time.

### Session

Every connection starts with a proof of the pair key. Each link gets commands and results only
after it succeeds.

| Write (Auth) | Notification | Meaning |
|---|---|---|
| `02` | `02 ‖ nonce16` | Challenge: 16 fresh random bytes, replacing that link's earlier one. `02` alone if there is no pair key or the write has extra bytes |
| `03 ‖ proof16` | `03 01` or `03 00` | Prove. The challenge is used up, success or not |

**Proof** = BLAKE2b with a 16-byte digest, keyed with the pair key, over the ASCII label
`RVL-AC-SESSION-v1` ‖ nonce.

On `03 01` the link becomes **the session**: any previous session's link is disconnected, the
command counter restarts, and the nonce becomes the session nonce. A failed proof changes
nothing, so a stranger cannot disturb the session. Other first bytes on Auth are ignored.

### Status

Notified on Status to the session's link once it subscribes, then on every change, whenever the
responding flag changes, and every 5 s. Reading Status returns the same 34 bytes to the session's
link and nothing to anyone else. The Home Assistant integration treats 15 s without a status as a
dead session: entities go unavailable and it reconnects.

| Byte | Field |
|---|---|
| 0 | Version, `01` |
| 1 | Flags: bit 0 responding, 1 power, 2 display on, 3 light on, 4 vent oscillating, 5 unit shows °C |
| 2 | Mode: 0 cool, 1 eco, 2 sleep, 3 turbo, 4 fan only, 5 dehumidify, 6 unknown |
| 3 | Fan level, 0 auto, 1–5, `FF` unknown |
| 4 | Setpoint, °F, `FF` unknown |
| 5 | Air temperature, °F, `FF` unknown |
| 6 | Battery voltage, whole volts, `FF` unknown |
| 7–8 | Low-voltage protection, 0.1 V units, u16 |
| 9–29 | The unit's raw 21-byte status frame |
| 30–33 | `00` |

Temperatures are always °F, converted if the unit shows °C. The mode and fan fields are what the
unit last reported; while power is off they are not meaningful (the Home Assistant integration
shows them as unknown). Responding clear means the sensor has no live unit; the rest is then the
last thing heard.

### Commands and results

**Command write** (Command), 22 bytes: `counter u32 ‖ op u8 ‖ value i8 ‖ mac16`.

**Command MAC** = BLAKE2b with a 16-byte digest, keyed with the pair key, over the ASCII label
`RVL-AC-CMD-v1` ‖ session nonce ‖ counter (u32) ‖ op ‖ value.

A command is accepted only from the session's link, with a valid MAC and a counter higher than the
last accepted one (the first is 1). Anything else, including a malformed body, gets BadAuth and
nothing reaches the AC.

| Op | Name | Value |
|---|---|---|
| 00h | Power | 0 off, 1 on |
| 01h | Mode | 0 cool, 1 eco, 2 sleep, 3 turbo, 4 fan only |
| 02h | Fan | 0 auto, 1–5 |
| 03h | Setpoint | °F, 61–86 |
| 04h | Step setpoint | +1 or −1, from the pending target if one is queued |
| 05h | Display | 0 off, 1 on |
| 06h | Vent oscillation | 0 static, 1 oscillating |
| 07h | Light | 0 off, 1 on |
| 08h | Unit scale | 0 °F, 1 °C |
| 09h | Clear preset | Ignored |

Cool when already cooling (with or without a preset) does nothing and succeeds. Cool or fan only
from off powers on first. A preset while off, and clear preset while off, are refused.

**Result**, notified on Command to the session's link: `counter u32 ‖ code u8`, where the counter
is that of the command (0 if a BadAuth reply was for a write under 4 bytes).

| Code | Name | Meaning |
|---|---|---|
| 00h | Ok | The unit's status shows it done |
| 01h | Refused | Not allowed: bad value, setpoint outside 61–86 °F or while the unit shows °C, unknown setpoint for a step, preset while off, queue full |
| 02h | Unreachable | No live unit, or it was lost before the command finished. Never queued for later |
| 03h | NotConfirmed | Sent twice and the status never showed it done, or still queued behind other commands 10 s after it arrived (then never sent) |
| 04h | BadAuth | Not from the session, bad MAC, counter reused or too low |

Status and result notifications are neither signed nor encrypted. They go only to the session's
link, but anyone sniffing the air can read them: the AC's state is not treated as private.
Commands are what the MAC protects.
