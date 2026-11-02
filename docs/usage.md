# Usage and configuration

This guide covers using the RV Level sensor once it is installed: reading it, zeroing it, and
setting it up for your coach and your blocks. To build and install one, see
[setup.md](setup.md).

## 1. Quick start

1. Install the sensor and power it from the RV-C bus ([setup.md](setup.md)).
2. Park on ground you know is level and **zero** it ([§3](#3-zeroing)).
3. Enter your coach's **wheelbase and track** and your **block height and count**
   ([§5](#5-block-leveling)). The defaults are 159.0″ wheelbase, 68.5″ track, 1.00″ blocks and
   10 blocks carried.
4. Optional: set up the **Home Assistant** dashboard ([§6](#6-home-assistant)).
5. Optional, AC bridge builds: bind the CountryModPro AC and pair Home Assistant
   ([§10](#10-air-conditioner-ac-bridge)).

Everything you set is saved in the sensor and kept through power cycles and firmware updates.

## 2. Reading the sensor

### Status LED

| LED | Meaning |
|---|---|
| Blue, blinking | Starting up: claiming an RV-C address |
| Green, solid | Running, and level |
| Green, blinking | Running, and at least one side is below level |
| Red | Fault: the MPU-6050 cannot be read, or the CAN bus is not working |

### Where the readings go

- **RV-C displays and leveling controllers** read the standard `LEVELING_SENSOR_STATUS`: pitch,
  roll, and which sides are below level.
- **A Firefly panel's LEVEL page** reads the per-wheel message and shows how many blocks go under
  each wheel.
- **Home Assistant** reads the Bluetooth broadcast and shows the same per-wheel blocks on its
  dashboard.

Pitch is positive when the nose is high. Roll is positive when the driver side is high.

### Two meanings of "level"

The sensor decides "level" in two ways, and near level they can disagree:

- **By angle**, for the LED, the standard RV-C status and the Bluetooth broadcast. A side is
  reported below level once the angle passes the **out-of-level** threshold (default 1.0°), and
  stays that way until the angle is back within the **within-level** threshold (default 0.5°). See
  [§4](#4-level-thresholds).
- **By blocks**, for the Firefly panel and the Home Assistant dashboard. The coach is level when
  no wheel needs a block. See [§5](#5-block-leveling).

## 3. Zeroing

Zeroing tells the sensor that the way it sits right now is level. Do it once after installing it,
and again if it is ever moved.

1. Park on ground you know is level (check with a spirit level on the floor, or use a site you
   trust). Level the coach first if you have to.
2. Make sure nobody is moving around inside, and wait a few seconds for the sensor to settle.
3. Zero it with any one of the methods below.

**From the Firefly panel:** press **Zero Level** (in the LEVEL SETUP popup).

**Over USB:** connect a computer, open `pio device monitor` (115200 baud), type `zero` and press
Enter. The sensor answers `zero set (pitch and roll)`. If it answers `zero rejected`, it is still
moving or has just started: wait until the status line shows `moving=0` and try again.

**Over RV-C** (from a Linux computer with `can-utils`, see [§8](#8-rv-c-commands)):

```bash
cansend can0 19FDE880#01FFFFFFFFFDFFFF   # zero pitch
cansend can0 19FDEA80#01FFFFFFFFFDFFFF   # zero roll
```

Each answers with an acknowledgement (`18E880BF`, first data byte `00`). A first byte of `03`
means the sensor was moving; wait and send it again.

After zeroing, both angles should read close to 0.0°.

## 4. Level thresholds

The thresholds decide when the LED, the standard RV-C status and the Bluetooth broadcast call a
side "below level". They do not affect the block counts.

| Setting | Default | Range |
|---|---|---|
| Out-of-level | 1.0° | 0–45°, and at least the within-level value |
| Within-level | 0.5° | 0–45° |

Pitch and roll have their own pair. Set them with the standard RV-C pitch and roll
configuration commands (1FDE8h for pitch, 1FDEAh for roll). Angles are encoded as
`(degrees + 200) × 128`, a 16-bit number sent low byte first. For example, to set pitch to 2.0°
out and 1.0° within:

```bash
cansend can0 19FDE880#0100658064FFFFFF
#                     01              sensor instance 1
#                       0065          out-of-level 2.0°  ((2+200)×128 = 6500h)
#                           8064      within-level 1.0°  ((1+200)×128 = 6480h)
#                               FF    no zero change
```

Use `19FDEA80` for roll. `FFFF` for either threshold leaves it unchanged. A value out of range
is refused (an acknowledgement with first byte `05`) and nothing changes.

## 5. Block leveling

The sensor works out how far to raise each wheel, and how many blocks that takes, to level the
coach. It keeps the highest wheel where it is and brings the others up to it.

### Settings

| Setting | Default | Range | What it is |
|---|---|---|---|
| Wheelbase | 159.0″ | 80–300″ | Front axle to rear axle, measured center to center |
| Track | 68.5″ | 40–110″ | Left wheels to right wheels, measured center to center |
| Block height | 1.00″ | 0.05–12.50″ | The thickness of one leveling block |
| Blocks carried | 10 | 0–250 | How many blocks you have |
| Tolerance | Auto | 0.00–2.00″, or Auto | A wheel that needs this much lift or less gets no block |

Measure wheelbase and track on your coach. The spec sheet usually lists the wheelbase. The
track is often not listed: measure between the centers of the tires on one axle.

### How the blocks are counted

For each wheel the sensor works out the lift needed, then rounds it to the **nearest whole
number of blocks**. A block is only added when it brings that wheel closer to level: with 1″
blocks, a 1.4″ lift gets 1 block and a 1.6″ lift gets 2. An exact half rounds down.

The **tolerance** is the lift below which a wheel gets no block. **Auto** (the default) sets it
to half the block height, which is exactly where the rounding changes anyway, so it has no
further effect, and it follows the block height if you change blocks. Set a fixed tolerance
larger than that if you want the sensor to ignore small lifts; for example, 0.75″ with 1″
blocks leaves any wheel needing 0.75″ or less at zero.

The coach counts as **level** when every wheel needs zero blocks. **Short on blocks** means the
total number of blocks needed is more than you carry. This is a conservative warning: stacking
blocks in a pyramid takes more blocks than the count of layers shows.

### From the Firefly panel

The LEVEL page shows the blocks for each wheel. The **LEVEL SETUP** popup sets:

- **Blocks carried** −/+ (one at a time),
- **Block height** −/+ (0.25″ at a time, from 0.25″ to 12.50″),
- **Tolerance** −/+ (0.05″ at a time; this changes it from Auto to a fixed value) and **AUTO**,
- **Zero Level** ([§3](#3-zeroing)).

The panel cannot set the wheelbase and track. Set those once over RV-C (below).

### Over RV-C

All block settings use one command, sent to the sensor at address BF: CAN ID `19EFBF80` from a
tool at address 80. Every frame starts `4C 56` ("LV"), followed by the operation.

| To do this | Send |
|---|---|
| Set wheelbase and track | `cansend can0 19EFBF80#4C5601` + wheelbase + track + `FF` (see below) |
| Set block height, blocks carried and tolerance | `cansend can0 19EFBF80#4C5602` + height + carried + tolerance + `FFFF` |
| Blocks carried +1 / −1 | `cansend can0 19EFBF80#4C5603FFFFFFFFFF` / `…#4C5604FFFFFFFFFF` |
| Block height +0.25″ / −0.25″ | `…#4C5606FFFFFFFFFF` / `…#4C5607FFFFFFFFFF` |
| Tolerance +0.05″ / −0.05″ (becomes fixed) | `…#4C5608FFFFFFFFFF` / `…#4C5609FFFFFFFFFF` |
| Tolerance back to Auto | `…#4C560AFFFFFFFFFF` |
| Wheelbase +0.5″ / −0.5″ (stops at 80″ and 300″) | `…#4C560BFFFFFFFFFF` / `…#4C560CFFFFFFFFFF` |
| Track +0.5″ / −0.5″ (stops at 40″ and 110″) | `…#4C560DFFFFFFFFFF` / `…#4C560EFFFFFFFFFF` |
| Restore the block defaults | `…#4C5605FFFFFFFFFF` |

Values are encoded as:

| Value | Encoding | Example |
|---|---|---|
| Wheelbase, track | tenths of an inch, 16 bits, low byte first | 159.0″ → 1590 = 0636h → `36 06` |
| Block height | twentieths of an inch (0.05″ steps) | 1.00″ → 20 = `14` |
| Blocks carried | count | 12 → `0C` |
| Tolerance | hundredths of an inch; `FE` = Auto | 0.25″ → 25 = `19` |

`FF` (or `FFFF`) in any field leaves that setting unchanged. Examples:

```bash
cansend can0 19EFBF80#4C56013606AD02FF     # wheelbase 159.0", track 68.5"
cansend can0 19EFBF80#4C5602140C19FFFF     # 1.00" blocks, 12 carried, tolerance 0.25"
cansend can0 19EFBF80#4C5602FFFFFEFFFF     # tolerance back to Auto, nothing else changed
cansend can0 19EFBF80#4C5602FF08FFFFFF     # 8 blocks carried, nothing else changed
```

The sensor answers every command with its current settings, as a `19EFBFxx` frame whose data
starts `04`:

```
04  36 06  AD 02  14  0C  19
    wheelbase track  height carried tolerance
```

If a value was out of range, nothing changes and the reply shows the old settings. With Auto
tolerance the last byte shows the tolerance in force (half the block height).

## 6. Home Assistant

The sensor broadcasts pitch and roll over Bluetooth LE in the BTHome format. Home Assistant's
built-in BTHome integration picks it up, and the project's **RV Level** integration adds the
blocks for each wheel, its settings and a dashboard. The Bluetooth broadcast is read-only:
zeroing and the sensor's own settings are done from the panel, USB or RV-C.

In AC bridge builds the same integration also controls the CountryModPro air conditioner,
over a paired connection rather than the broadcast ([§10](#10-air-conditioner-ac-bridge)).

### Installing

1. Make sure Home Assistant has a Bluetooth adapter within range of the sensor. It should
   discover **RV Level** (through BTHome) under **Settings → Devices & services**. Add it there.
2. Install **Mushroom** from HACS if you don't have it. The dashboard needs it.
3. In HACS, open **⋮ → Custom repositories**, add `https://github.com/dbergl/rv-level-sensor` as type
   **Integration**, then install **RV Level** and restart Home Assistant.
4. Go to **Settings → Devices & services → Add integration**, choose **RV Level**, then
   **Leveling sensor**. Pick the sensor if it asks, then enter your wheelbase and track and check the
   driver's side. **RV Level** appears in the sidebar.

### Moving from the YAML install

If you installed the older YAML package, remove its files from Home Assistant's configuration
directory:

- `packages/rv_level.yaml`
- `custom_templates/rv_level.jinja`
- `dashboards/rv_level.yaml`
- `www/rv_level/`

Restart Home Assistant, then add the integration as above. It takes over the entity IDs the
package used, so your history and automations keep working. Block height, tolerance and blocks
carried start at their defaults (the old `input_number` values aren't carried over), so set them
again on the dashboard. Wheelbase and track are entered while adding the integration.

### Manual install (without HACS)

Copy the `custom_components/rv_level/` folder from a clone of this project into the
`custom_components` folder in Home Assistant's configuration directory. Restart Home Assistant
and add the integration as in steps 2 and 4 above.

To use the older YAML package instead, `tools/ha_install.sh` copies its files into the
configuration directory and turns on packages in `configuration.yaml`:

```bash
tools/ha_install.sh user@ha-host /path/to/ha/config   # over ssh
tools/ha_install.sh /path/to/ha/config                # on the Home Assistant machine
```

If your `configuration.yaml` already has a `homeassistant:` section, it asks you to add this line
under it yourself:

```yaml
  packages: !include_dir_named packages
```

Restart Home Assistant afterwards. Don't run the package and the integration together.

### Using the dashboard

Each wheel shows how many blocks to put under it and the lift. Set your wheelbase, track, block
height, tolerance and blocks carried on the dashboard or on the **RV Level** device page. The
lift unit follows Home Assistant's unit system as it is set when you add the integration.

The integration does its own block calculation from these settings. **Keep them the same as the
sensor's** ([§5](#5-block-leveling)), or Home Assistant and the panel will show different counts.
Home Assistant has no Auto tolerance; set it to half your block height or less.

**Sensor fault** shows when the sensor reports a fault, and also when Home Assistant stops
hearing it (for example, when the coach's power is off or it is out of range). Home Assistant only
checks every few minutes, so this takes about 3 minutes to appear.

The dashboard is managed by the integration and updates with it, so it can't be edited in the UI.

### Adding the dashboard by hand

If the integration could not create the dashboard (Home Assistant shows a repair for it):

1. Go to **Settings → Dashboards → Add dashboard** and create a new one.
2. Open it, then **⋮ → Edit → ⋮ → Raw configuration editor**.
3. Paste in the contents of
   [`custom_components/rv_level/dashboard.yaml`](../custom_components/rv_level/dashboard.yaml)
   and save.

## 7. Restarting and factory defaults

**Restart:** type `reboot` on the USB serial monitor, or send an RV-C GENERAL_RESET to the
sensor's address:

```bash
cansend can0 197FBF80#FDFFFFFFFFFFFFFF
```

**Factory defaults:** this resets the thresholds, the block settings **and the zero**. Re-zero
the sensor afterwards ([§3](#3-zeroing)).

```bash
cansend can0 197FBF80#DFFFFFFFFFFFFFFF
```

The factory defaults are 1.0° / 0.5° thresholds, zero at 0°, 159.0″ wheelbase, 68.5″ track,
1.00″ blocks, 10 carried and Auto tolerance. To reset only the block settings and keep the zero,
use the block defaults command instead ([§5](#over-rv-c)).

Replace `BF` in these commands with the sensor's address if it claimed a different one (see
below). GENERAL_RESET sent to all devices (`FF`) is ignored.

## 8. RV-C commands

The examples in this guide use `can-utils` on a Linux computer connected to the RV-C bus, with
the interface named `can0` and brought up at 250 kbit/s:

```bash
sudo ip link set can0 up type can bitrate 250000
candump -ta can0      # watch the traffic
```

The examples use `80` as the computer's own address; any address not used by another device on
the bus works.

**The sensor's address.** The sensor claims an address from BFh down to B0h, starting at BFh,
and remembers it. It is shown as `SA=` on the USB serial monitor (in decimal: 191 is BFh), and is
the last two hex digits of its frames in `candump`. The block commands in [§5](#over-rv-c) are
always accepted at BF even if the sensor has a different address. The zeroing, threshold, request
and GENERAL_RESET commands go to its actual address.

**Reading settings back.** Ask for the current settings with a request:

```bash
cansend can0 18EABF80#E9FD01FFFFFFFFFF    # pitch thresholds (reply 19FDE9BF)
cansend can0 18EABF80#EBFD01FFFFFFFFFF    # roll thresholds  (reply 19FDEBBF)
cansend can0 18EABF80#BFEF01FFFFFFFFFF    # block settings   (reply 19EFBFBF, record 04)
```

### Messages the sensor sends

| CAN ID | Message | When |
|---|---|---|
| `18EE00BF` | ADDRESS_CLAIM | At start-up and on request |
| `0DFFEBBF` | LEVELING_SENSOR_STATUS: pitch, roll, sides below level | Every second, and on change |
| `19EFBFBF` | Per-wheel blocks, four records: 01 attitude and level, 02 blocks per wheel, 03 lift per wheel, 04 settings | Every second, and on change |
| `19FDE9BF`, `19FDEBBF` | Pitch and roll threshold settings | On request and after a change |
| `19FECABF` | DM_RV: diagnostics and fault status | Every 5 seconds (every second while faulted), and on request |
| `18E8xxBF` | Acknowledgement | In reply to zero, threshold and GENERAL_RESET commands |

Message layouts are in the [protocol reference](protocol.md).

## 9. Troubleshooting

| Symptom | Check |
|---|---|
| LED red, `fault=1` on the serial monitor | The MPU-6050 is not answering. Check its four wires and that VCC is on 3.3 V. The sensor retries every 2 seconds and recovers by itself. |
| LED red, `busoff=1` | The CAN bus is not working. Check CAN-H, CAN-L and ground, the termination ([setup.md §3](setup.md#3-wiring)), and that the bus runs at 250 kbit/s. |
| LED keeps blinking blue | It has not claimed an address. Check the CAN wiring and that the bus runs at 250 kbit/s. |
| Pitch or roll has the wrong sign | The axis map does not match the mounting. See [setup.md §4](setup.md#4-mounting-and-orientation). |
| Zeroing is rejected | The sensor is moving, or started less than a few seconds ago. Wait for `moving=0` and hold everything still. |
| Readings drift after moving the sensor | Re-zero it ([§3](#3-zeroing)). |
| Panel and Home Assistant show different block counts | Their settings differ. Match the RV Level settings in Home Assistant to the sensor's ([§6](#using-the-dashboard)). |
| Panel says level but the LED blinks (or the reverse) | Expected near level: they use different rules ([§2](#two-meanings-of-level)). |
| RV Level dashboard shows "Custom element doesn't exist: mushroom-template-card" | Install **Mushroom** from HACS ([§6](#installing)). |
| Home Assistant does not find the sensor | Its Bluetooth adapter must be within range. Check `BLE_ENABLED` was not turned off in the build. |
| The panel's AC goes blank or stale | The sensor has no live link to the AC (the console shows `AC link=0` or `responding=0`). Check the AC's power and range, and that nothing else (the vendor app, `countrymodpro`) holds its connection. It reconnects by itself, waiting up to 30 s between tries ([§10](#10-air-conditioner-ac-bridge)). |
| Console says `no AC unit (KT...) found` | The AC is off, out of range or busy with another app. Free it and type `pair`. |
| Console says `AC candidate … rejected` | That device connected but did not behave like the AC (no service, or no status within 5 s). Type `pair` to scan again; if it keeps picking the wrong one, move closer to your AC. |
| Home Assistant says the AC bridge is not in pairing mode | The window is closed. Type `pair` on the console and submit again ([§10](#adding-the-ac-in-home-assistant)). |
| AC entities are unavailable in Home Assistant | Home Assistant has no session with the sensor: power, range, or the sensor was `unpair`ed or re-paired from elsewhere. In the last case remove the AC entry and add it again. |
| AC setpoint changes fail | The unit is showing °C. Turn `switch.countrymodpro_display_in_celsius` off. |

## 10. Air conditioner (AC bridge)

Builds with the AC bridge (the default `esp32` build, or `esp32_ac` without leveling; see
[setup.md §7](setup.md#7-build-options)) also act as the Bluetooth controller for a
CountryModPro rooftop AC. The sensor holds the one Bluetooth connection the AC allows, and both
the Firefly panel (over RV-C) and Home Assistant (over Bluetooth) control the AC through it. The
panel needs no Home Assistant, broker or other computer.

Both show what the AC last reported. A button press is a request: it is done when the AC's own
status shows it. If the sensor cannot reach the AC, it stops sending the panel its status, so the
panel's AC goes stale after 30 seconds instead of showing a frozen state, and Home Assistant shows
the AC as not responding. A press made while the AC is unreachable fails and is not remembered
for later.

Not supported: dry (dehumidify) mode, fresh air, heating, and the unit's countdown timer. A
heat or dry request from the panel is ignored. Only the Firefly v66 panel has been checked.

### Pairing

Two things are bound to the sensor on purpose, never automatically from the air: **which AC
unit** it controls, and **which Home Assistant** may command it. Both happen in the **pairing
window**, which is 120 seconds long. Open it yourself, at the coach, from the USB serial monitor
(`pio device monitor`, 115200 baud):

| Command | What it does |
|---|---|
| `pair` | Opens the window. If no AC unit is bound, it scans for 10 seconds for one named `KT…`, picks the strongest signal and tries it. Home Assistant can pair while the window is open |
| `forget-ac` | Forgets the bound AC unit, disconnects it and stops reconnecting. `pair` binds one again |
| `unpair` | Forgets the paired Home Assistant and drops its connection. The AC unit stays bound |

The window also opens by itself at start-up when the sensor has neither an AC unit nor a Home
Assistant stored: a new sensor, or after `forget-ac` and `unpair`. A sensor that has either one
stored stays closed at start-up; type `pair`. The window closes by itself after 120 seconds, or as
soon as Home Assistant has paired (`pairing window closed` on the console).

**Binding the AC unit.** Power the AC, close the vendor app, and make sure nothing else is
connected to it (the unit takes one connection at a time, including Home Assistant's
`countrymodpro` integration). The console shows:

```
pairing window open (120 s)
scanning 10 s for the AC unit (KT...)
AC candidate KT2026010000007 02:12:34:56:78:9A (RSSI -61 dBm): connecting to verify
AC unit bound: KT2026010000007 02:12:34:56:78:9A
```

Check that the name is your unit's. The candidate is saved only after the sensor has connected,
found the AC's service and received a valid status frame within 5 seconds. Otherwise the console
says `AC candidate … rejected: …` and that device is never tried again. `no AC unit (KT...)
found` means nothing was heard: check power and range and type `pair` again. Once bound, the
sensor connects to that unit only, so it can never wander onto a neighbor's AC. Each second the
console also prints a status line starting `AC link=`.

**Pairing Home Assistant** is done from Home Assistant, below. The sensor accepts the pairing
only while the window is open, replaces any earlier pairing (even one whose Home Assistant is
connected), and answers `Home Assistant paired`.

### Adding the AC in Home Assistant

1. Install the integration ([§6](#installing)). Home Assistant must have a Bluetooth adapter in
   range of the sensor.
2. Type `pair` on the sensor's console (or power on a new sensor).
3. **Settings → Devices & services → Add integration → RV Level → Air conditioner (AC bridge)**.
   Choose the sensor, and press Submit. "Not in pairing mode" means the window had closed: type
   `pair` and submit again.

Home Assistant keeps a connection to the sensor and reconnects by itself. The AC can be added
with or without a leveling entry, and a leveling-only coach needs nothing of this.

The device is named **CountryModPro**, with the same entity IDs as the `countrymodpro`
integration it replaces:

| Entity | What |
|---|---|
| `climate.countrymodpro` | Off, cool, fan only; presets none, eco, sleep, boost (turbo); fan auto and 1–5; setpoint 61–86 °F; air temperature |
| `switch.countrymodpro_power`, `binary_sensor.countrymodpro_power` | Power |
| `select.countrymodpro_operating_mode`, `sensor.countrymodpro_mode` | Cool, eco, sleep, turbo, fan only |
| `select.countrymodpro_fan_level` | Auto, 1–5 |
| `number.countrymodpro_setpoint` | Setpoint, 61–86 °F |
| `sensor.countrymodpro_air_temperature` | Air temperature, °F |
| `sensor.countrymodpro_battery_voltage` | Battery voltage, V |
| `sensor.countrymodpro_low_voltage_protection` | Low-voltage cut-off setting (diagnostic) |
| `sensor.countrymodpro_status_frame` | The unit's last raw status frame as hex (diagnostic) |
| `switch.countrymodpro_display_in_celsius` | The unit's own °C/°F scale |
| `switch.countrymodpro_light`, `switch.countrymodpro_display`, `switch.countrymodpro_vent_oscillation` | Light, display, vent oscillation |
| `binary_sensor.countrymodpro_ac_responding` | On while the sensor has a live link to the AC |
| `binary_sensor.countrymodpro_unit_in_celsius` | Problem: the unit shows °C, so setpoint changes are refused |

The fresh air switch and the dehumidify option no longer exist. Check that no automation uses
them.

Things to know:

- **The AC is the truth.** A control changes only when the AC reports it; there is no guess in
  between. A command that fails shows as an error in Home Assistant.
- **While the AC is off,** preset, operating mode and fan show as unknown (the unit's mode and
  fan are not meaningful then). The setpoint and air temperature stay shown. A mode the sensor
  cannot decode shows as unknown too (and n/a on the panel).
- **Cool while a preset is on does nothing.** Clear a preset with preset *none* (or the operating
  mode select's *cool*, which does the same).
- **A unit in °C:** the sensor never changes the unit's scale on its own. Turn
  `switch.countrymodpro_display_in_celsius` off to put it back in °F.
- **Availability.** If Home Assistant loses the sensor (no power, out of range), every AC entity
  becomes unavailable, except `…_ac_responding`, which turns off. If the sensor loses only the AC,
  the entities show unknown and `…_ac_responding` turns off.

### Moving from the `countrymodpro` integration and the Pi

Do these in order. Between steps 2 and 3, the panel is not controlling the AC.

1. Check your automations and dashboards for the `countrymodpro` fresh air switch and dehumidify
   option, which are dropped.
2. Flash the AC bridge build ([setup.md §6](setup.md#6-building-the-firmware)). If the Pi is
   still running its virtual AC, stop it first so two senders do not both send instance 7.
3. Stop and disable the Pi's `virtual-ac` service, and remove `virtual_ac_bridge` from Home
   Assistant.
4. Remove `countrymodpro` from Home Assistant. This frees the AC's single connection (and its
   entity IDs). The first-start scan will probably have found nothing while Home Assistant held the
   AC: type `pair` now.
5. Let the sensor connect to the AC (`AC unit bound`, then `AC link=1 responding=1`) and check
   that the panel's AC page works.
6. Add the AC in Home Assistant (above). Check the entity IDs and your pet-mode automation.

### Alert when the AC stops during pet mode

The pet-mode automation itself stays yours; the integration supplies the entities it needs.
This one notifies if the AC stops responding, goes unavailable, or is turned off while a helper
named `input_boolean.pet_mode` is on:

```yaml
automation:
  - alias: Pet mode AC alert
    mode: single
    triggers:
      - trigger: state
        entity_id: binary_sensor.countrymodpro_ac_responding
        to: "off"
        for: "00:05:00"
      - trigger: state
        entity_id: climate.countrymodpro
        to: "unavailable"
        for: "00:05:00"
      - trigger: state
        entity_id: climate.countrymodpro
        to: "off"
        for: "00:02:00"
    conditions:
      - condition: state
        entity_id: input_boolean.pet_mode
        state: "on"
    actions:
      - action: notify.notify
        data:
          title: "Pet mode: check the AC"
          message: "The AC is {{ states('climate.countrymodpro') }} and responding is {{ states('binary_sensor.countrymodpro_ac_responding') }}."
```

Use your own notify service. Adjust the delays: the responding sensor can flicker if the AC link
drops and recovers within a minute or so.

### Limits worth knowing

- **Pairing has no confirmation code.** Anyone who connects to the sensor while the window is open
  can pair, so open it (`pair`) only yourself, at the coach, and use it at once. After pairing,
  every command needs the pairing key; strangers in range cannot send them.
- **Status is not private.** The AC's state (power, temperature, battery voltage) travels
  unencrypted and can be read by anyone with a sniffer in range. Only commands are protected.
- **There is no automatic re-pairing.** If the sensor loses its pairing (`unpair`, a new board, or
  flash erased), the Home Assistant entry keeps trying and fails. Remove the AC entry and add it
  again with the window open. Pairing again from another Home Assistant replaces the first, even
  while the first is connected; it is disconnected and its AC entities become unavailable.
- **One Home Assistant** is paired at a time.
- **The panel's power button while the AC shows off** was not captured on the coach, so whether
  it turns the AC on is unconfirmed. The cool and fan buttons and Home Assistant do power it on
  by design; use those if the power button does nothing.
- **Power use.** AC bridge builds run the processor at 240 MHz all the time. The extra current has
  not been measured yet; it will be measured at the bench and added here. The leveling-only build
  keeps 80 MHz.
