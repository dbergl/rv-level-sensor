# RV-C Leveling Sensor

An ESP32 RV-C node that measures coach pitch and roll with an MPU-6050 and reports them:

- on RV-C as `LEVELING_SENSOR_STATUS` (1FFEBh), with the pitch/roll configuration DGNs and the
  DGs every RV-C node must support (ADDRESS_CLAIM, PRODUCT_ID, DM_RV, requests, ACK/NAK,
  GENERAL_RESET);
- on RV-C as a proprietary per-wheel block-leveling message (1EFBFh) for a touchscreen panel;
- over Bluetooth LE as a BTHome v2 broadcast for Home Assistant.

The Home Assistant side is a custom integration installed through HACS
([usage.md §6](docs/usage.md#6-home-assistant)). Its dashboard needs the Mushroom cards, also
installed from HACS.

Firmware updates can be sent over Bluetooth when enabled in the build (off by default).

## Documentation

- [Setup](docs/setup.md): parts, wiring, mounting, the case, building and installing.
- [Usage](docs/usage.md): zeroing, thresholds, block leveling, Home Assistant, troubleshooting.
- [Protocol reference](docs/protocol.md): RV-C messages, BTHome advert, Bluetooth update service.
- [Development](docs/development.md): unit tests, hardware tests, bench checks with can-utils.

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware entry point, `config.h` (build options), hardware drivers in `src/hal/` |
| `lib/` | Hardware-independent logic: RV-C (`rvc`), attitude and blocks (`level`), BTHome, OTA, LED |
| `test/` | Host unit tests (`pio test -e native`) |
| `case/` | OpenSCAD case model and STLs |
| `custom_components/` | Home Assistant integration (`rv_level`), installed through HACS |
| `tests/` | Integration tests (see [Development](docs/development.md)) |
| `homeassistant/` | Older YAML package, dashboard and tests |
| `tools/` | Bluetooth update, Home Assistant install, hardware test scripts |

## Quick start

```bash
pio test -e native            # host unit tests
pio run -e esp32 -t upload    # build and flash over USB
pio device monitor            # 115200 baud
```

## License

[MIT](LICENSE)
