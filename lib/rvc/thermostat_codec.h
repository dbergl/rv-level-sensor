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
// Byte 1: 2-bit fields, oscillate bits 0-1, display 2-3, recirculate 4-5 (1 on, 0 off), 6-7 n/a.
// Bytes 2-3: the unit's low-voltage protection, u16 LE, 0.1 V/bit (0xFFFF unknown).
void packOptionsStatus(uint8_t instance, bool oscillate, bool display, bool recirculate,
                       uint16_t lvpDeciV, uint8_t out[8]);  // 0FFA1
// Manual, max fan 100 %, max output 100 %, fan and output 0.5 %/bit (0xFF n/a), dead band 1.0 degC.
void packAcStatus(uint8_t instance, uint8_t fanRaw, uint8_t outputRaw, uint8_t out[8]);  // 1FFE1

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
  uint8_t recirculate;
};
OptionsCommand unpackOptionsCommand(const uint8_t in[8]);  // 0FFA0

struct AcCommand {
  uint8_t instance;
  uint8_t fanRaw;     // 0.5 %/bit, > 200 = no change
  uint8_t outputRaw;  // 0.5 %/bit, > 200 = no change
};
AcCommand unpackAcCommand(const uint8_t in[8]);  // 1FFE0

}  // namespace rvc
