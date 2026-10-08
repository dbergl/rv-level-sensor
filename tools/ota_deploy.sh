#!/usr/bin/env bash
# Build the firmware here and install it on the sensor in the coach, through the Pi.
#
#   tools/ota_deploy.sh                     # over Bluetooth (the sensor stays in place)
#   tools/ota_deploy.sh --usb               # first install: over USB to the Pi's /dev/ttyUSB0
#   tools/ota_deploy.sh -- --info           # extra arguments go to ble_ota.py
#
# Options: --host user@host (default pi@raspberrypi.local), --bus can1.
set -euo pipefail

host=pi@raspberrypi.local
bus=can1
usb=0
extra=()
while [[ $# -gt 0 ]]; do
  case $1 in
    --host) host=$2; shift 2 ;;
    --bus) bus=$2; shift 2 ;;
    --usb) usb=1; shift ;;
    --) shift; extra=("$@"); break ;;
    *) echo "usage: $0 [--host user@host] [--bus canN] [--usb] [-- ble_ota.py args]" >&2; exit 2 ;;
  esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/.pio/build/esp32"
remote=rv-level-ota

# --info, --open-window and --connect-test need no build.
needs_build=1
for x in "${extra[@]+"${extra[@]}"}"; do
  case $x in --info|--open-window|--connect-test) needs_build=0 ;; esac
done
if (( needs_build || usb )); then (cd "$root" && pio run -e esp32); fi

ssh "$host" "mkdir -p $remote"
files=("$root/tools/ota/ble_ota.py" "$root/tools/ota/ota_proto.py" "$root/tools/hwtest/common.py")
if (( needs_build || usb )); then files+=("$build/firmware.bin"); fi
scp -q "${files[@]}" "$host:$remote/"
ssh "$host" "cd $remote && { test -x venv/bin/python || python3 -m venv venv; } &&
  venv/bin/pip install -q --disable-pip-version-check bleak python-can esptool"

if (( usb )); then
  boot_app0=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
  scp -q "$build/bootloader.bin" "$build/partitions.bin" "$boot_app0" "$host:$remote/"
  # NVS (0x9000) is not written, so settings and the zero survive.
  ssh "$host" "cd $remote && venv/bin/python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
    --before default_reset --after hard_reset write_flash \
    0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin"
else
  args=(--bus "$bus" "${extra[@]+"${extra[@]}"}")
  if (( needs_build )); then args=(firmware.bin "${args[@]}"); fi
  ssh -t "$host" "cd $remote && venv/bin/python -u ble_ota.py ${args[*]}"
fi
