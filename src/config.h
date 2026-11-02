#pragma once
#include <cstdint>

#include "attitude.h"

// Build options (platformio.ini envs): leveling sensor, CountryModPro AC bridge, or both.
#ifndef RVL_LEVELING
#define RVL_LEVELING 1
#endif
#ifndef RVL_AC_BRIDGE
#define RVL_AC_BRIDGE 1
#endif

// Compile-time configuration for the RV-C leveling sensor.
namespace cfg {

constexpr bool LEVELING_ENABLED = RVL_LEVELING != 0;
constexpr bool AC_BRIDGE_ENABLED = RVL_AC_BRIDGE != 0;
static_assert(LEVELING_ENABLED || AC_BRIDGE_ENABLED, "enable leveling, the AC bridge, or both");

// Identity (PRODUCT_ID = MAKE*MODEL*<MAC>*)
constexpr const char* MAKE = "DIY";
constexpr const char* MODEL = !AC_BRIDGE_ENABLED ? "LEVEL-MPU6050" : !LEVELING_ENABLED ? "AC-COUNTRYMOD" : "LEVEL-AC";
constexpr uint16_t MANUFACTURER_CODE = 0;  // no SAE/RVIA code assigned
constexpr uint8_t SENSOR_INSTANCE = 1;
constexpr uint8_t DSA = 81;  // leveling system (no DSA exists for a standalone sensor)
constexpr uint8_t SA_RANGE_HIGH = 191;
constexpr uint8_t SA_RANGE_LOW = 176;

// Mounting: which MPU-6050 axis points to the front of the coach and which to the driver side.
constexpr level::Axis AXIS_FORWARD = level::Axis::PosX;
constexpr level::Axis AXIS_DRIVER = level::Axis::PosY;
static_assert(level::isValidAxisMap(AXIS_FORWARD, AXIS_DRIVER),
              "AXIS_FORWARD and AXIS_DRIVER must be different, non-opposite axes");

// Timing / filtering
constexpr uint32_t SAMPLE_PERIOD_MS = 10;
// 80: lowest clock that keeps Bluetooth advertising; APB (CAN, I2C, UART) stays 80 MHz. With the AC
// bridge, 240 all the time: the unit link and HA connect at any moment (see CPU_MHZ_UPDATE).
constexpr uint32_t CPU_MHZ = AC_BRIDGE_ENABLED ? 240 : 80;
// While the update window is open: at 80 MHz the BT controller misses new connections (the central
// gives up with 0x3E, "connection failed to be established"); at 240 MHz it accepts them.
constexpr uint32_t CPU_MHZ_UPDATE = 240;
constexpr uint32_t STATUS_MIN_GAP_MS = 100;
constexpr uint32_t STATUS_HEARTBEAT_MS = 1000;
constexpr float STATUS_DEADBAND_DEG = 0.05f;
constexpr uint32_t WHEEL_HEARTBEAT_MS = 1000;  // LEVEL_WHEEL_STATUS (1EFBFh), all four records
constexpr uint32_t WHEEL_MIN_GAP_MS = 250;     // on-change bursts; the panel repaints no faster
constexpr float WHEEL_DEADBAND_DEG = 0.05f;    // displayed angle holds until the reading moves this far
constexpr float FILTER_TAU_S = 1.0f;
constexpr float MOTION_GYRO_DPS = 0.5f;

// BTHome BLE broadcast (Home Assistant)
constexpr bool BLE_ENABLED = true;
constexpr const char* BLE_NAME = "RV Level";
constexpr uint32_t BLE_ADV_INTERVAL_MS = 250;  // each reading (BLE_REFRESH_MS) still goes out ~2x
constexpr uint32_t BLE_REFRESH_MS = 500;
// Re-send unchanged advert data with a new packet ID: recovers from a BLE host reset.
constexpr uint32_t BLE_REAPPLY_MS = 10000;
// Leaves out the Bluetooth update service: the sensor never accepts a connection, and a
// GENERAL_RESET Reboot/Enter Bootloader restarts it instead. Updates are then USB only.
constexpr bool BLE_OTA_DISABLED = true;

// AC bridge: how long the pairing window (console "pair", or at boot while nothing is paired)
// stays open to bind the AC unit and accept a Home Assistant pairing.
constexpr uint32_t PAIR_WINDOW_MS = 120000;

// SK Pang ESP32 CAN-Bus board pins
constexpr int PIN_CAN_TX = 25;
constexpr int PIN_CAN_RX = 26;
constexpr int PIN_SDA = 23;  // board routes JP2-12 SDA to IO23 (schematic rev B)
constexpr int PIN_SCL = 22;
constexpr int PIN_LED_R = 2;
constexpr int PIN_LED_G = 15;
constexpr int PIN_LED_B = 4;
constexpr uint8_t LED_BRIGHTNESS_PCT = 3;  // status LED brightness, 0-100 % (0 = off)

}  // namespace cfg
