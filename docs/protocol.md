# Protocol reference

All multi-byte integers are little endian. "n/a" fields are all ones (`FF` / `FFFF`).

## RV-C

250 kbit/s, 29-bit IDs: `priority << 26 | DGN << 8 | source address`. The examples below assume
the sensor has address BFh.

### Address and identity

- Claims an address from BFh (191) down to B0h (176), starting at the last address it held.
  At start-up it sends a request for ADDRESS_CLAIM (`18EABFFE`), then ADDRESS_CLAIM
  (`18EE00BF`) 250 ms later, and starts normal traffic.
- NAME: arbitrary-address capable, manufacturer code 0, identity number from the low 21 bits of
  the MAC address.
- PRODUCT_ID (0FEEBh): `DIY*LEVEL-MPU6050*<MAC>*`, sent as a multi-packet (TP.CM BAM then
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
| Byte 1 bits 4–5, Reboot/Enter bootloader | ACK `00`, opens the Bluetooth update window (restarts instead if Bluetooth is not running) |
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

## BTHome (Bluetooth LE)

Name `RV Level`, service data UUID FCD2h, BTHome v2, unencrypted, regular interval
(device info `40`).

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

The sensor accepts connections only while the update window is open. A GENERAL_RESET
Reboot/Enter bootloader opens it for 5 minutes, and each control or data write moves the
deadline to 5 minutes later. While it is open the advert is connectable and the CPU runs at
240 MHz instead of 80 MHz. RV-C and BTHome reporting continue during a transfer.

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
| 01h BEGIN | size u32, SHA-256 (32 bytes) | Start a transfer to the spare slot. Restarts one in progress |
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

**Data write**: offset u32, then image bytes; the whole write must fit in 512 bytes. A write at
the wrong offset is dropped and answered with one RESEND per gap. Data past `size` gives
ERR_BAD_SIZE and aborts. A disconnect aborts the transfer. The uploader keeps at most 16 KB in
flight beyond the last PROGRESS.

**Trial boot.** A new image starts on trial. It confirms itself once it has run for 30 s, claimed an RV-C
address and read the MPU-6050. Otherwise it rolls back to the previous image at 60 s.
