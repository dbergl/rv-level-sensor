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

### Installing

1. Make sure Home Assistant has a Bluetooth adapter within range of the sensor. It should
   discover **RV Level** under **Settings → Devices & services**. Add it there.
2. Install **Mushroom** from HACS if you don't have it. The dashboard needs it.
3. In HACS, open **⋮ → Custom repositories**, add this repository's GitHub URL as type
   **Integration**, then install **RV Level** and restart Home Assistant.
4. Go to **Settings → Devices & services → Add integration** and choose **RV Level**. Pick the
   sensor if it asks, then enter your wheelbase and track and check the
   driver's side. **RV Level** appears in the sidebar.

### Moving from the YAML install

If you installed the older YAML package, remove its files from Home Assistant's configuration
directory:

- `packages/rv_level.yaml`
- `custom_templates/rv_level.jinja`
- `dashboards/rv_level.yaml`
- `www/rv_level/`

Restart Home Assistant, then add the integration as above. It takes over the entity IDs the
package used, so your history and automations keep working.

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
