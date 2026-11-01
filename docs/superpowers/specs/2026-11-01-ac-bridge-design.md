# AC bridge: CountryModPro rooftop AC on RV-C and Home Assistant

Status: draft for review, 2026-11-01

## Intent

Today the CountryModPro rooftop AC reaches the Firefly panel through a long chain:

```
Firefly panel ─RV-C─ Pi (virtual AC, instance 7) ─MQTT─ HA bridge ─ HA countrymodpro ─BLE─ AC
```

Every link has to be up for the panel to control the AC. The goal is to put the whole bridge
in the ESP32 that already sits on the RV-C bus as the leveling sensor:

```
Firefly panel ─RV-C─ ESP32 (leveling + AC bridge) ─BLE─ AC
                       │
                       └─BLE─ Home Assistant (rv_level integration)
```

The panel then controls the AC with no Pi, broker or Home Assistant involved. Home Assistant
stays a full control surface for when the owner is away from the panel and for automations,
most importantly a **pet mode** that keeps the coach comfortable while a pet is inside and the
owner is away.

## Goals

1. The ESP32 stands in for RV-C AC/thermostat **instance 7** for the Firefly panel (v66 build),
   replacing the Pi's virtual AC.
2. The ESP32 holds the one Bluetooth connection the AC allows, polls it, and is the only thing
   that talks to it.
3. **The AC is the single source of truth.** The panel and HA both show what the unit last
   reported. A command from either side is done only when the unit's own status shows it.
4. The `rv_level` HA integration gains an AC device that replaces the `countrymodpro`
   integration, with the same controls the panel has.
5. HA can tell when the AC stops responding, so a pet-mode automation can notify the owner.
6. Control of the AC through the ESP32 requires a one-time pairing with HA.
7. Two build options: **AC bridge** on/off and **leveling** on/off. Default build: both on.
8. Leveling to HA is unchanged: the BTHome broadcast keeps working in every build that has
   leveling.

## Non-goals

- Dry (dehumidify) mode and fresh air / recirculate. The unit accepts the commands but the
  hardware doesn't implement them (confirmed by the manufacturer), and the v66 panel has no
  buttons for them.
- Heating. The unit can't heat. A heat request from the panel is not acted on.
- The unit's countdown timer (its cancel command was never captured).
- More than one AC, or Velit heaters and other Velit ACs.
- Building the pet-mode automation itself. It stays an HA automation; this project supplies
  the entities it needs.
- Supporting panel builds other than v66 (v59 was not checked).

## Anti-goals (would be a failure even if it technically works)

- The panel and HA disagree about the AC, or undo each other's changes.
- Showing a state the unit didn't report (for example "cooling" while the unit is off), or
  showing stale state as live.
- Anyone in Bluetooth range being able to control the AC.
- Code from `countrymodpro`, `velit-hass` or the Pi bridge ending up in this repo (see
  Clean room).
- A Home Assistant outage affecting the AC on the panel.
- Any change to leveling behavior in the default build.

## Clean room

Licensing of the existing projects is unclear: `countrymodpro` is a GPL-2.0 project
inspired by `velit-hass`, and the Pi bridge is a heavily modified version of another RV-C
bridge project. This project is MIT. So:

- **Only signal facts carry over**: byte positions, values, command codes, timing limits and
  observed device behavior. They are all in the Signal sheet below.
- **All logic is written fresh** from this document. Whoever implements this works from the
  Signal sheet and does **not** open `countrypro/`, `velit-hass`, `countrymodpro` or the Pi
  bridge sources while writing code or tests.
- Structure, naming, state machines and test cases follow this repo's own style, not those
  projects'.
- `countrypro/` is currently untracked in this repo's working tree. Add it to `.gitignore`
  so it can never be committed.

## Signal sheet

### A. CountryModPro unit over Bluetooth

Observed on a 48 V CountryModPro rooftop unit; command values were confirmed by writing them
to the unit one at a time.

**Link**
- Advertises with a local name starting `KT` (`KT<yyyymm><serial>`, e.g. `KT2026010000007`).
- GATT service `0000ffe0-0000-1000-8000-00805f9b34fb`; notifications from `…ffe1…`, writes to
  `…ffe2…` (write with response).
- Accepts **one** client at a time. While the vendor app is connected, nothing else can be.
- At least **400 ms** between commands. No reply within ~3 s counts as a failed command.

**Command frame** (to the unit)

```
5A 5A  len  product  func  data  checksum  0D 0A
```
- `len` = number of data bytes + 5 (all commands here have 1 data byte, so `len` = 6).
- `product` = `01`.
- `checksum` = low byte of the sum of every byte before it (from the first `5A` through `data`).

**Commands** (the complete allow-list; nothing else may ever be sent)

| func | data | meaning |
|---|---|---|
| 01 | 00 | status query (read only) |
| 01 | 01 / 02 | power off / on |
| 02 | 01 | mode cool (auto sub-mode) |
| 02 | 02 | mode fan only |
| 02 | 04 / 05 / 06 | mode eco / sleep / turbo (sub-modes of cooling) |
| 03 | 61–86 | setpoint, °F (only while the unit is in °F, see below) |
| 04 | 01–05 | fan level 1–5 |
| 04 | 06 | fan auto |
| 0A | 01 / 02 | display off / on |
| 45 | 00 / 01 | vent static / oscillating |
| 1C | 01 / 02 | light on / off |
| 20 | 01 / 02 | unit's own temperature scale: °C / °F |

Mode data `03` never appeared in the vendor app's traffic and must not be sent. Mode `07`
(dehumid) and func `44` (fresh air) exist but are out of scope.

**Status frame** (from the unit)

The unit answers every query and every command with one status frame:

```
idx  0  1  2   3   4   5  6  7  8   9  10 11 12 13 14 15  16  17  18  19 20
    5A 5A b2  b3  b4  b5 00 00 b8  b9 00 b11 00 00 00 b15 b16 b17 ck 0D 0A
```

- Always **21 bytes**. Byte 2 is not a length.
- `ck` (byte 18) = low byte of the sum of bytes 0–17.
- Arrives split over notifications (seen as 10 + 10 + 1 bytes); frames must be reassembled
  and resynchronized on `5A 5A` if a fragment is lost.

| Field | Where | Meaning |
|---|---|---|
| power | b2 bit 0x80 | set = on |
| display | b2 bit 0x01 | set = display on |
| fan level | (b2 >> 1) & 7 | 0 = auto, 1–5 |
| base mode | b2 bits 0x30 | used only when b3 has no sub-mode bit: both set = fan only; 0x20 only = dehumid; otherwise cool |
| sub-mode | b3 & 0xB0 | 0x10 sleep, 0x20 eco, 0x80 turbo, 0x00 none (see base mode). Any other pattern = unknown |
| fresh air | b3 bit 0x01 | (out of scope) |
| light | b3 bit 0x02 | set = on |
| vent oscillating | b3 bit 0x08 | set = oscillating |
| — | b3 bit 0x40 | set while the vendor app is connected; ignore |
| air temperature | b4 | in the unit's current scale |
| battery voltage | b8 | whole volts (plausible 20–70) |
| low-voltage protection | b9 | low byte of the setting in 0.1 V (high byte assumed 1: value = (256 + b9) / 10 V) |
| timer running | b11 == 1 | (out of scope) |
| scale | b16 | 1 = unit in °F, 0 = unit in °C |
| setpoint | b17 | in the unit's current scale |

Not decoded: b5, b15, b2 bit 0x08.

**Behavior**
- Changing the mode can change the fan level on the unit (turbo goes to fan 5, fan-only → cool
  went from 5 to 3). Always take the fan level from the next status frame.
- In °C mode the unit reports whole °C for air temperature and setpoint. The setpoint command's
  units in °C mode were never captured, so **setpoint writes are refused while the unit is in °C**.
- The setpoint range is 61–86 °F. 61 °F is the lowest seen on the unit's controller; 86 °F is the
  top of the related Velit range and is unconfirmed on this unit.
- Plausible read ranges (treat anything else as unknown): air temperature 30–130 °F,
  setpoint 50–99 °F.

### B. Firefly panel (v66) on RV-C, instance 7

Confirmed from the v66 panel firmware and from captures on 2026-10-23.

**General**
- RV-C, 250 kbit/s, 29-bit IDs: priority(3) | DGN(17) | source address(8). Byte 0 of every
  message below is the instance, `07`. Multi-byte values are little-endian.
- The panel matches only the DGN and byte 0. **It ignores the sender's source address**, so the
  ESP32 can keep its claimed address. Two senders of instance-7 status fight (last frame wins).
- The panel's commands come from SA `9F` and are broadcast (PDU2, no destination).
- The panel does not need ADDRESS_CLAIM or PRODUCT_ID from the AC node.
- Every AC status entry goes stale if not refreshed within **30 s**; the panel then shows every
  AC highlight as off.
- Temperatures: uint16, 1/32 °C per bit, offset −273 °C. `FFFF` = not available, `FExx` = error.
  The panel displays °F as `(raw − 8736) × 9 / 160 + 32` with the fraction dropped, so a raw value
  a hair under a whole °F shows one degree low: encode by **rounding up** to the next raw value.
- Percent: uint8, 0.5 % per bit, max 200 (= 100 %). `FF` = not available.
- 2-bit fields: `00` off, `01` on, `11` not available / no change.

**What the panel reads**

| DGN | Name | Fields the panel uses |
|---|---|---|
| 1FFE2 | THERMOSTAT_STATUS_1 | byte 1 bits 0–3 mode (0 off, 1 cool, 4 fan only); bits 4–5 fan mode (00 auto, 01 on, 11 n/a); bits 6–7 schedule (send 11). Byte 2 fan speed %. Bytes 3–4 heat setpoint, bytes 5–6 cool setpoint. Byte 7 `FF` |
| 1FF9C | THERMOSTAT_AMBIENT_STATUS | bytes 1–2 room temperature; bytes 3–7 `FF` |
| 1FEFA | THERMOSTAT_STATUS_2 | byte 3: bits 0–1 sleep, 2–3 eco, 4–5 turbo (01 = on), bits 6–7 `11`. `(byte3 & 0x3F) == 0` lights the panel's Auto icon. Other bytes `FF` |
| 0FFA1 | proprietary option status | byte 1: bits 0–1 oscillate, bits 2–3 display (01 = on), bits 4–7 `1111`. Other bytes `FF` |

- Fan speed buttons 1–5 light at raw thresholds `28`, `50`, `78`, `A0`, `C8` (20–100 %),
  cumulatively: level N is raw `N × 0x28`.
- Fan mode bits `00` light the Auto fan button.
- With the AC off: mode 0, fan mode `11` and fan speed 0 light neither Auto nor any speed bar,
  and 1FEFA with every field `FF` lights none of the four preset icons.
- The panel does **not** read 1FFE1 AIR_CONDITIONER_STATUS; don't send it.

**What the panel sends** (all from SA `9F`, byte 0 = `07`)

| DGN | Name | Button → content |
|---|---|---|
| 1FEF9 | THERMOSTAT_COMMAND_1 | Byte 1 bits 0–3 mode: power (while the AC shows on) sends 0; cool 1; fan 4; heat 2; dry 6. `F` = no change. Bits 4–5 fan mode: Auto sends `00`. Byte 2 fan speed: buttons 1–5 send 20/40/60/80/100 %. Bytes 3–4 heat setpoint, bytes 5–6 cool setpoint: setpoint up/down send `FFFA` / `FFF9` **in the cool field**, one press = one 1 °F step |
| 1FEF8 | THERMOSTAT_COMMAND_2 | byte 2 low 6 bits: `10` turbo, `00` auto, `04` eco, `01` sleep. The same code is sent on every press |
| 0FFA0 | proprietary option command | byte 1 2-bit fields at the same positions as 0FFA1: a press sends `01` in its field (display `F7`, oscillate `FD`) and `11` elsewhere |

Open on the panel side: what the power button sends while the AC shows **off** was not
captured. Capture it before relying on it (see Builder notes).

## Design decisions

These were decided with the owner:

- **One board does both jobs** for now. The leveling-off option is for flexibility (for example
  a dedicated AC board later), not a current plan.
- **Retire `countrymodpro`** and fold the AC into the `rv_level` integration.
- **Pairing is required** for AC control from HA.
- **HA keeps every control the panel has.** HA is used away from the panel and for automations.
- **The panel and HA both reflect the unit's real status.**
- **HA notifies** if the AC stops while pet mode is on (as an HA automation using the new
  entities).
- **Keep the whole `countrymodpro` entity set**, same ids and same values (see the entity table),
  including the HA-only light and °C switches. Only the out-of-scope features are dropped.
- **HA runs on a Raspberry Pi in the coach and uses the Pi's own Bluetooth adapter** (no
  ESPHome proxy), so a persistent HA → ESP32 connection is fine.
- **Running at 240 MHz all the time is acceptable** when the AC bridge is on.

These are my calls (the owner can override):

- **HA entity ids stay the same as today's `countrymodpro` ones** (`climate.countrymodpro`,
  `sensor.countrymodpro_battery_voltage`, …), so existing automations, pet mode and dashboards
  keep working.
- **Pairing is our own application-level pairing**, not OS-level Bluetooth bonding. OS pairing
  is unreliable through HA, and especially through ESPHome Bluetooth proxies.
- **Both build options off is a compile error.**
- **Unknown or unreachable means unknown.** If the ESP32 can't reach the unit, it stops sending
  instance-7 status so the panel goes stale after 30 s rather than showing a frozen state as
  live, and HA shows the AC as not responding.
- **Commands are never queued across an outage.** A command that arrives while the unit is
  unreachable fails right away; it is not replayed later.
- **Choosing the unit is deliberate.** The ESP32 binds to a unit only during the same
  deliberate pairing window used for HA, picking the strongest `KT…` signal and logging which
  one. The address is then stored in NVS and the ESP32 never connects to any other unit. This
  keeps it from landing on a neighbor's AC at a campground.
- **Pairing uses a key exchange and per-message authentication**: X25519 during pairing, then
  every command carries a MAC and a counter so a link can't be hijacked or replayed after the
  check (monocypher, already in `lib/`, covers both). Status notifications need not be private.
- **Cool while a preset is active is a no-op**; clearing a preset takes preset auto/none.
- **Rapid setpoint presses count from the pending target**, not the last reported value, so no
  press is lost. Once the unit's status catches up, the reported value is the truth again.
- **A unit in °C is a visible problem**, not a silent failure: HA gets a "unit in °C" problem
  sensor (setpoint changes are refused until the unit is back in °F). The bridge never switches
  the scale on its own; the °C switch in HA lets the owner fix it remotely.
- **The AC device can be added in HA without a leveling device**, so the leveling-off build is
  usable on its own.

## How it fits together

### Firmware

```
              ┌───────────── ESP32 ─────────────┐
 panel ─RV-C─▶│ RV-C node ──▶ AC bridge state ◀─┼─ HA control service (paired) ◀─ HA
       ◀─RV-C─│     │            │      ▲       │
              │ leveling      BLE client         │
              │     │            │               │
              │ BTHome adverts   ▼               │
              └──────────────── AC ──────────────┘
```

- **AC link** (new, `src/hal/`): NimBLE client. Connects only to the unit bound during the
  pairing window (address in NVS), subscribes, reconnects with backoff. A console command
  forgets the unit.
- **AC protocol** (new, `lib/`, host-tested): command frame building, the allow-list, frame
  reassembly and status decoding, all from Signal sheet A.
- **Bridge state** (new, `lib/`, host-tested): the last decoded unit status plus a single
  serialized command queue fed by both the panel and HA. It enforces the 400 ms gap and the
  3 s timeout, polls the unit every few seconds when idle, and treats each reply as the new truth.
  It turns intents into unit commands:
  - panel/HA mode off → power off; cool → power on if needed, then mode cool (no-op if already
    cooling in a preset); fan only → mode fan only.
  - preset eco/sleep/turbo → that mode (it implies cooling). Pressing the active preset again,
    or "auto"/none, → mode cool. A preset while the AC is off is ignored. *(My call: the panel
    sends the same code each press, so a repeat clears it.)*
  - fan auto → fan `06`; fan N % → nearest of 1–5.
  - setpoint up/down → pending target (or the reported setpoint if none is pending) ± 1 °F,
    clamped 61–86; absolute setpoints round to whole °F. Refused while the unit is in °C.
  - light and °C scale (HA only) → `1C` and `20` commands.
  - display/oscillate press → toggle from the unit's current reported state.
  - heat and dry → not sent; status stays as the unit reports it.
- **RV-C AC/thermostat** (new codec in `lib/rvc/`, host-tested): encodes 1FFE2, 1FF9C, 1FEFA
  and 0FFA1 from bridge state and decodes 1FEF9, 1FEF8 and 0FFA0, per Signal sheet B.
  Sent on every change and at least every 5 s. Messages for other instances are ignored.
- **HA control service** (new GATT service on the ESP32's existing NimBLE server): status
  notifications (decoded unit state, AC-responding flag) and commands, both only after the
  pairing check. Our own UUIDs and message format, documented in `docs/protocol.md`.
- **Pairing**: during a deliberately opened pairing window, HA and the ESP32 run an X25519 key
  exchange and both store the resulting key (ESP32 NVS, HA config entry). The same window binds
  the AC unit. Afterwards every command carries a MAC over a counter and the payload; the ESP32
  rejects anything unauthenticated or replayed. One paired HA at a time; re-pairing replaces it.
  *(How the window is opened is the builder's call; it must be a deliberate act at the coach,
  for example the serial console or a short window after power-up while unpaired.)*
- **Existing RV-C node**: today's `rvc::Node` mixes the base node duties (address claim,
  PRODUCT_ID, DM_RV, requests, GENERAL_RESET) with leveling. The leveling-off build needs those
  separated so the base node works without leveling.

### Build options

- `AC_BRIDGE_ENABLED` and `LEVELING_ENABLED` (names are the builder's call), both on by default,
  in `src/config.h` style or as PlatformIO build flags.
- Leveling off: no MPU-6050, no leveling DGNs, no wheel status, no leveling BTHome data.
- AC off: firmware behaves exactly as today. This build must stay byte-for-byte equivalent in
  behavior.
- PRODUCT_ID model string reflects what the build does.

### Home Assistant (`rv_level`)

- Leveling: unchanged (BTHome).
- New AC device, added through the config flow: discover the ESP32's control service, pair,
  store the key. Holds a persistent connection through the HA Pi's own Bluetooth adapter and
  reconnects on loss.
- The AC device can be added with or without a leveling device (today's config flow requires
  a BTHome leveling device and the manifest allows a single entry; both need to change).
- Device named so the entity ids match today's `countrymodpro` ones. Keep the whole set with
  the same values, so existing automations, pet mode and dashboards keep working (entity list
  and values from the `countrymodpro` README, an interface description):

| Entity | What |
|---|---|
| `climate.countrymodpro` | hvac modes off / cool / fan_only; presets none / eco / sleep / boost (boost = turbo) on cool; fan modes auto, 1–5; setpoint 61–86 °F; current temperature = unit air temperature. No `hvac_action` (the unit doesn't report compressor state) |
| `switch.countrymodpro_power` | power |
| `binary_sensor.countrymodpro_power` | power (read only) |
| `select.countrymodpro_operating_mode` | cool, eco, sleep, turbo, fan only (dehumid dropped) |
| `sensor.countrymodpro_mode` | operating mode (read only) |
| `select.countrymodpro_fan_level` | auto, 1–5 |
| `number.countrymodpro_setpoint` | setpoint, 61–86 °F |
| `sensor.countrymodpro_air_temperature` | air temperature, °F |
| `sensor.countrymodpro_battery_voltage` | battery voltage, whole volts |
| `sensor.countrymodpro_low_voltage_protection` | low-voltage protection setting (diagnostic) |
| `sensor.countrymodpro_status_frame` | raw 21-byte status frame as hex (diagnostic) |
| `switch.countrymodpro_display_in_celsius` | the unit's own °C/°F scale |
| `switch.countrymodpro_light` | light |
| `switch.countrymodpro_display` | display |
| `switch.countrymodpro_vent_oscillation` | vent oscillation |
| `binary_sensor` "AC responding" (new) | the ESP32 has a live link to the unit and recent status |
| `binary_sensor` "unit in °C" (new, problem) | setpoint changes are refused until the unit is back in °F |

- Dropped: `switch.countrymodpro_fresh_air_intake` and the dehumid option (not implemented by
  the hardware). Before cut-over, check that no automation uses them.
- The builder confirms exact state strings and attributes against the running HA instance's
  entity states (not the `countrymodpro` code) before cut-over.

- Entities go unavailable when HA loses the ESP32; the "AC responding" sensor goes off when the
  ESP32 loses the unit. Pet mode watches both.
- A command fails visibly in HA if the unit's status doesn't confirm it.

## Cut-over

1. Flash the ESP32 with the AC bridge build while the Pi still runs (the ESP32 should not send
   instance-7 status until it has a unit to talk to, but stop the Pi first to be safe).
2. Stop and disable the Pi's `virtual-ac` service. Remove `virtual_ac_bridge` from HA.
3. Remove `countrymodpro` from HA (frees the unit's single connection).
4. ESP32 connects to the unit; confirm the panel's AC page works.
5. Add the AC device in `rv_level`, pair, confirm entity ids and pet-mode automation.

## Constraints

- ESP32 (esp32dev) on the SK Pang CAN board, Arduino framework, NimBLE-Arduino 1.4.
- The firmware runs at 80 MHz to save power; at 80 MHz the BT controller misses new
  connections. With the AC bridge on, run at 240 MHz all the time (accepted by the owner). Measure
  and document the power cost. AC-off builds keep today's 80 MHz behavior.
- The NimBLE host is shared by BTHome advertising, the (optional) update service, the AC client
  and the HA control service. Default max connections is 3: unit + HA + update service fits.
- Bluetooth range from the sensor's mounting spot to the rooftop unit, and to the HA Pi, is
  unverified.

## Testing

- Host unit tests (`pio test -e native`) for: command frames and checksum, the allow-list,
  reassembly from arbitrary fragments including lost bytes, status decoding of every field and
  out-of-range values, intent → command mapping, RV-C encode/decode for every DGN above
  including the round-up temperature rule, stale/unreachable behavior, and build-option
  combinations compiling.
- Integration tests for the HA side in `tests/`, matching the existing style.
- On the coach: panel buttons each produce the right unit change and the panel reflects it;
  changes at the unit's own remote reach the panel and HA; pulling the unit's power makes the
  panel go stale and HA report not responding.

## Docs

Update `docs/protocol.md` (instance-7 DGNs, HA control service, pairing), `docs/usage.md`
(pairing, cut-over, build options), `docs/setup.md` (build options), and the README summary.

## Builder notes (left to the builder)

- Exact module and file names, the control service's UUIDs and message format, the pairing
  window mechanism, poll interval (a few seconds), reconnect backoff.
- Capture what the panel's power button sends while the AC shows off, and the cool button's
  effect while off, before finalizing the off → on path.
- Whether the AC bridge should also put a few AC values in the BTHome advert (cheap read-only
  fallback if the HA connection drops).
