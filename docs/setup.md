# Setup: hardware and firmware

This guide covers what you need to build an RV Level sensor, how to wire and mount it, and how
to build and install the firmware. Once it is running, see [usage.md](usage.md) for day-to-day
use and configuration.

## 1. What it does

The sensor is a small RV-C node. It measures the coach's pitch and roll and reports them in
three ways:

- **On RV-C** as the standard `LEVELING_SENSOR_STATUS` (1FFEBh), for any RV-C display or leveling
  controller.
- **On RV-C, per wheel,** with a proprietary message that tells a touchscreen panel how many
  blocks to put under each wheel.
- **Over Bluetooth LE** in the BTHome v2 format, which Home Assistant picks up.

It is powered from the RV-C bus, and it can be updated over Bluetooth once installed.

## 2. Hardware requirements

### Parts

| Part | Qty | Notes |
|---|---|---|
| [SK Pang ESP32 CAN-Bus board](https://www.skpang.co.uk/) | 1 | ESP32 with an MCP2562 CAN transceiver, a 12 V regulator, a Qwiic socket and an RGB LED. The firmware's pin map is for this board (schematic rev B). |
| GY-521 breakout (MPU-6050) | 1 | The accelerometer and gyroscope. Any GY-521 works. |
| 4 short wires | 1 set | GY-521 to the board, under 30 cm. Solder them, or use a Qwiic-to-female-jumper cable. |
| RV-C drop cable | 1 | CAN-H, CAN-L, ground and +12 V to the board's 4-pin connector J3. |
| 3D-printed case | 1 | Optional. See [§5](#5-case). |

### Tools and computer

- A soldering iron (the GY-521 wires are soldered in the case).
- A USB cable for the board, for the first firmware install.
- A computer running Linux, macOS or Windows with [PlatformIO](https://platformio.org/) and git.
  See [§6](#6-building-the-firmware).

### Optional

- **A Linux computer on the coach's RV-C bus with Bluetooth**, such as a Raspberry Pi with a CAN
  HAT. It is needed for Bluetooth firmware updates ([§8](#8-updating-over-bluetooth)) and is useful
  for testing with `can-utils`. The CAN interface must run at 250 kbit/s.
- **Home Assistant** with a Bluetooth adapter within range of the sensor, for the phone and tablet
  dashboard. See [usage.md §6](usage.md#6-home-assistant).
- **A Firefly touchscreen panel** with the LEVEL page, for block-by-block leveling on the panel.

## 3. Wiring

Pin assignments come from the SK Pang schematic
([ESP32_CAN_rev_B.pdf](https://cdn.shopify.com/s/files/1/0563/2029/5107/files/ESP32_CAN_rev_B.pdf)).
Check them against your board's silkscreen before powering up.

```
 GY-521 (MPU-6050)            SK Pang ESP32 CAN-Bus board
 ┌───────────┐
 │ VCC  ●────┼──────────────── 3.3V   (J5 Qwiic pin 2, or JP2 pin 1)
 │ GND  ●────┼──────────────── GND    (J5 Qwiic pin 1, or JP2 pin 2)
 │ SCL  ●────┼──────────────── SCL    (J5 Qwiic pin 4, or JP2 pin 11)  = GPIO22
 │ SDA  ●────┼──────────────── SDA    (J5 Qwiic pin 3, or JP2 pin 12)  = GPIO23
 │ XDA  ●    │  not connected
 │ XCL  ●    │  not connected
 │ AD0  ●    │  not connected (I²C address 0x68)
 │ INT  ●    │  not connected
 └───────────┘

 RV-C bus / power             J3 (4-pin CAN/power connector)
   CAN-L ───────────────────── pin 1
   CAN-H ───────────────────── pin 2
   GND   ───────────────────── pin 3
   +12V  ───────────────────── pin 4
```

- **Power the GY-521 from 3.3 V, not 5 V.** The ESP32's I/O is 3.3 V only.
- J5 is a Qwiic (JST-SH, 1 mm pitch) socket and the GY-521 has 0.1″ pins. Use a
  Qwiic-to-female-jumper cable on the bench, or solder to header JP2. In the case, solder the wires
  directly to the GY-521: there is no room for jumper plugs.
- Keep the I²C wires short (under about 30 cm). The bus runs at 400 kHz.
- **Always connect ground** (J3 pin 3) to the RV-C ground, not just CAN-H and CAN-L.
- **Termination:** fit the 120 Ω jumper (JP1) **only** if the sensor is at the physical end of the
  RV-C bus. RV-C buses are normally terminated at both ends already, and a third terminator can
  stop the bus working.

## 4. Mounting and orientation

The sensor measures the tilt of whatever it is screwed to, so mount it:

- **Rigidly**, on a flat, horizontal surface that moves with the coach's chassis (a floor or a
  solid shelf, not a cabinet door or anything that flexes).
- **Lid up, with the case's FRONT arrow pointing to the front of the coach.**

It does not need to be perfectly level: you set the zero once it is installed
([usage.md §3](usage.md#3-zeroing)). It does need to be square to the coach, because the firmware
assumes which way is forward.

Without the case, the default orientation is the MPU-6050's **+X axis toward the front** of the
coach and **+Y toward the driver side** (both are printed on the GY-521). If you mount it any
other way, change `AXIS_FORWARD` and `AXIS_DRIVER` in `src/config.h` before building
([§7](#7-build-options)).

## 5. Case

`case/level_case.scad` is a parametric OpenSCAD model. `case/level_case_bottom.stl` and
`case/level_case_lid.stl` are ready to print. Outside size is 92 × 50 × 17 mm (74 mm across the
mounting ears).

**Printing.** PETG, no supports. Print the lid top-face down (the STL is already oriented that
way). The board screws pull along the layers, so print for layer strength: 4 walls, nozzle at the
hot end of the filament's range (about 245 °C), part fan 30–50 % with any auxiliary fan off, and
walls at 60–100 mm/s. Fast-printer PLA-style profiles leave the standoffs weak enough to split.

The lid's engraving is on its first layer. The model already allows for first-layer squish (a
0.4 mm chamfer round the edge and engraving 0.1 mm wider on the first layer), so set the
slicer's elephant's foot compensation to 0. Use a first-layer line width of about 0.42 mm so the
small terminal labels stay open, and a smooth build plate for crisp lettering.

**Screws.**

| Screws | Qty | For |
|---|---|---|
| M2.5 × 5 mm pan head | 6 | The two boards. Not 6 mm, which can bottom out before clamping. They tap their own thread: drive them slowly and stop when snug. |
| M3 × 10 mm flat head | 2 | The lid. |
| #6 or #8 (3.5–4 mm) countersunk wood screws, ½″–¾″ long | 4 | Mounting the case. Snug them gently: overtightening can split a PETG tab. |

**Assembly.**

1. **ESP32 board first.** Lower it in a little back from the connector end, then slide it toward
   the connector slot so its corners pass under the lid retainer beads. Screw it down.
2. **GY-521.** Mount it with its holes toward the ESP32 and its VCC pin toward the "VCC" mark on
   the floor. Solder the four wires ([§3](#3-wiring)) and screw it down.
3. **Lid.** Hook the connector end in first and press down until it clicks, then fit the two
   screws. If the snap is too tight or too loose in your material, change `retainer_r` in the
   model by 0.1–0.2 mm and reprint the bottom.

To re-export the STLs after editing the model:

```bash
openscad -o case/level_case_bottom.stl -D 'part="bottom"' case/level_case.scad
openscad -o case/level_case_lid.stl    -D 'part="lid"'    case/level_case.scad
```

## 6. Building the firmware

### Prerequisites

- **git**, to clone the project. The firmware version is taken from git, so build from a clone
  rather than a downloaded archive (an archive builds, but reports its version as `unknown`).
- **PlatformIO Core** (the `pio` command), either on its own
  (`pip install platformio`, or see the [install guide](https://docs.platformio.org/en/latest/core/installation/))
  or as part of the PlatformIO extension for VS Code. PlatformIO downloads the ESP32 toolchain,
  the Arduino framework and the NimBLE library on the first build.
- **A USB serial driver** for the board, if your operating system does not already have one.
- Optional: [**uv**](https://docs.astral.sh/uv/), to run the Python test and update tools without
  installing their dependencies yourself.
- **A firmware signing key.** Bluetooth updates must be signed, and the build embeds your public
  key. Create the key pair once with `uv run tools/ota/sign.py keygen` (in `~/.config/rv-level/`),
  and back up `ota_ed25519.pem`: anyone with it can update the sensor, and without it only a
  USB install can change the key. The firmware build stops if there is no key.

### Build and test

```bash
git clone <repository-url> level-sensor
cd level-sensor
pio test -e native     # host unit tests: no hardware needed
pio run -e esp32       # build the firmware
```

The first build takes a few minutes while PlatformIO downloads its packages. The firmware image
is written to `.pio/build/esp32/firmware.bin`.

### First install over USB

Connect the board to your computer with USB, then:

```bash
pio run -e esp32 -t upload     # flash the firmware
pio device monitor             # watch its output (115200 baud); Ctrl+C to quit
```

PlatformIO finds the serial port by itself. If you have more than one, add
`--upload-port /dev/ttyUSB0` (or `COM3` on Windows) to the upload and `-p` to the monitor.

On start-up the monitor shows a line like:

```
RV-C level sensor DIY*LEVEL-MPU6050*A0B1C2D3E4F5* firmware 1a2b3c4 2026-09-29 14:02:11 (slot 0)
```

followed by a status line every second:

```
SA=191 pitch=+0.42 roll=-1.07 low[D F P R]=1000 layers[FL FR RL RR]=1 0 2 1/10 fault=0 moving=0 busoff=0 drops=0
```

`SA` is the RV-C address it claimed. `fault=1` means it cannot read the MPU-6050: check the
wiring. `busoff=1` means the CAN bus is not working: check CAN-H/CAN-L, ground and termination.
Without an RV-C bus connected the sensor still runs, but it cannot claim an address.

Settings (zero, thresholds and block configuration) are stored in the ESP32's flash and survive
firmware updates. To wipe them and start from the defaults, erase the flash before uploading:

```bash
pio run -e esp32 -t erase && pio run -e esp32 -t upload
```

## 7. Build options

Compile-time settings live in `src/config.h`. Rebuild and reinstall after changing any of them.

| Setting | Default | Change it when |
|---|---|---|
| `AXIS_FORWARD`, `AXIS_DRIVER` | `PosX`, `PosY` | The MPU-6050 is not mounted with +X forward and +Y toward the driver side. Use `PosX`, `NegX`, `PosY`, `NegY`, `PosZ` or `NegZ`. The two must be different axes; the build fails otherwise. |
| `SENSOR_INSTANCE` | 1 | You have more than one leveling sensor on the bus. |
| `SA_RANGE_HIGH`, `SA_RANGE_LOW` | 191, 176 (BFh–B0h) | The range of RV-C addresses it may claim. It starts at the top. |
| `BLE_ENABLED` | `true` | `false` turns off the Bluetooth broadcast and Bluetooth updates. |
| `BLE_NAME` | "RV Level" | The Bluetooth name Home Assistant shows. |
| `LED_BRIGHTNESS_PCT` | 3 | Status LED brightness, 0–100 % (0 = off). |
| `PIN_*` | SK Pang board | You use a different ESP32 board or wiring. |

## 8. Updating over Bluetooth

Once the sensor is installed, later versions can be sent over Bluetooth without removing it.
This needs a Linux computer (a Raspberry Pi works) that has:

- a SocketCAN interface on the coach's RV-C bus at 250 kbit/s (for example `can1`),
- Bluetooth, within range of the sensor,
- Python 3 with `venv`, and ssh access from the computer you build on.

From the build computer:

```bash
tools/ota_deploy.sh --host pi@my-pi --bus can1            # build, then update over Bluetooth
tools/ota_deploy.sh --host pi@my-pi --bus can1 -- --info  # show the running version
```

`--host` defaults to `pi@raspberrypi.local` and `--bus` to `can1`. The script builds the firmware,
copies it and the uploader to the Pi (setting up a Python environment there on the first run),
then:

1. opens the sensor's update window with an RV-C GENERAL_RESET "Reboot/Enter Bootloader" command.
   The sensor only accepts Bluetooth connections for 5 minutes after one;
2. sends the image (about 20 s);
3. waits for the sensor to restart and confirm the new version.

The script signs the image on the build computer with your private key; only the signature goes
to the Pi. The sensor refuses images signed with any other key.

If the sensor runs firmware from before signed updates, install this version once with
`tools/ota_deploy.sh -- --unsigned`. After that, unsigned images are refused.

The sensor keeps working on RV-C and Bluetooth during the transfer. Connecting can take several
tries; the uploader retries up to 10 times.

A new version starts on trial. It keeps itself once it has run for 30 s, claimed its RV-C address
and read the MPU-6050; otherwise it goes back to the previous version after 60 s. A corrupt or
interrupted transfer leaves the old version running.

**Installing over USB through the Pi.** If the sensor is plugged into the Pi's USB port
(`/dev/ttyUSB0`), `tools/ota_deploy.sh --usb` builds and flashes it over USB instead. Saved
settings are kept.

## 9. Checking the installation

With the sensor on the RV-C bus, the LED should blink blue briefly while it claims an address,
then go green. A Linux computer with `can-utils` on the same bus shows its traffic:

```bash
sudo ip link set can0 up type can bitrate 250000
candump -ta can0
```

Expect `18EE00BF` (address claim) once at start-up, then `0DFFEBBF` (pitch and roll) every
second and a burst of four `19EFBFBF` frames (per-wheel blocks) every second. The last two hex
digits are the sensor's address; `BF` is the default.

To confirm the orientation, raise the front of the coach (or tip the sensor nose up on the bench):
pitch should increase. Raise the driver side: roll should increase. If either is backwards, fix
`AXIS_FORWARD` / `AXIS_DRIVER` ([§7](#7-build-options)). `uv run tools/hwtest/tilt_test.py` walks
you through this check.

Next: zero the sensor and set it up. See [usage.md](usage.md).
