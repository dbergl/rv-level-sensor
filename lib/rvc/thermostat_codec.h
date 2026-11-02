#pragma once
#include <cstdint>

namespace rvc {

// Setpoint step requests the panel sends in the cool setpoint field of THERMOSTAT_COMMAND_1.
constexpr uint16_t SETPOINT_UP = 0xFFFA;
constexpr uint16_t SETPOINT_DOWN = 0xFFF9;

// 1/32 degC per bit, offset -273 degC. Rounds up: the panel truncates when it shows degF.
uint16_t encodeTempF(int f);
// Nearest whole degF; the caller checks raw < 0xFE00 first.
int decodeTempF(uint16_t raw);

struct ThermoStatus {
  uint8_t instance;
  uint8_t mode;     // 0 off, 1 cool, 4 fan, 6 dehumid
  uint8_t fanBits;  // 0 auto, 1 on, 3 n/a
  uint8_t fanRaw;   // 0.5 % per bit
  int coolF;        // -1 = n/a
};

void packThermoStatus1(const ThermoStatus& s, uint8_t out[8]);  // 1FFE2
void packAmbient(uint8_t instance, int f, uint8_t out[8]);      // 1FF9C

enum class Preset : uint8_t { None, Eco, Sleep, Turbo };
void packThermoStatus2(uint8_t instance, bool on, Preset p, uint8_t out[8]);  // 1FEFA
void packOptionsStatus(uint8_t instance, bool oscillate, bool display, uint8_t out[8]);  // 0FFA1

struct ThermoCommand1 {
  uint8_t instance;
  uint8_t mode;  // 0x0F = none
  uint8_t fanBits;
  uint8_t fanRaw;
  uint16_t coolRaw;
};
ThermoCommand1 unpackThermoCommand1(const uint8_t in[8]);  // 1FEF9

struct ThermoCommand2 {
  uint8_t instance;
  bool has;
  uint8_t code;  // low 6 bits of byte 2
};
ThermoCommand2 unpackThermoCommand2(const uint8_t in[8]);  // 1FEF8

struct OptionsCommand {
  uint8_t instance;
  uint8_t oscillate;  // 2-bit: 0 off, 1 on/press, 3 none
  uint8_t display;
};
OptionsCommand unpackOptionsCommand(const uint8_t in[8]);  // 0FFA0

}  // namespace rvc
