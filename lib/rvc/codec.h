#pragma once
#include <cstdint>

#include "level_logic.h"

namespace rvc {

constexpr uint16_t U16_NA = 0xFFFF;   // data not available / no change
constexpr uint16_t U16_ERR = 0xFFFE;  // error / out of range

// Angle: 1/128 deg per bit, offset -200 deg, valid -200..+300 deg.
uint16_t encodeAngle(float deg);
float decodeAngle(uint16_t raw);

void put16(uint8_t* p, uint16_t v);
uint16_t get16(const uint8_t* p);

struct SensorStatusMsg {
  bool valid;
  level::LevelBits bits;
  float pitchDeg;
  float rollDeg;
  uint8_t instance;
};
void packSensorStatus(const SensorStatusMsg& m, uint8_t out[8]);

// Shared layout of the pitch/roll config status and command DGNs.
struct AxisConfigMsg {
  uint8_t instance;
  uint16_t outRaw;     // encoded angle, U16_NA = no change
  uint16_t withinRaw;  // encoded angle, U16_NA = no change
  uint8_t setZero;     // 2-bit field: 0 no action, 1 set zero, 3 not available
};
void packAxisConfig(const AxisConfigMsg& m, uint8_t out[8]);
AxisConfigMsg unpackAxisConfig(const uint8_t in[8]);

struct DmRvMsg {
  uint8_t dsa;
  bool fault;  // yellow lamp + SPN/FMI when true
  uint32_t spn;
  uint8_t fmi;
};
void packDmRv(const DmRvMsg& m, uint8_t out[8]);

void packAck(uint8_t code, uint8_t instance, uint8_t ackedSa, uint32_t ackedDgn, uint8_t out[8]);

struct RequestMsg {
  uint32_t dgn;
  uint8_t instance;
};
RequestMsg unpackRequest(const uint8_t in[8]);
void packRequest(uint32_t dgn, uint8_t out[8]);

// ADDRESS_CLAIM field (spec 3.3.3), arbitrary-address-capable set.
uint64_t makeName(uint32_t serial21, uint16_t manufacturer11);
void packName(uint64_t name, uint8_t out[8]);
uint64_t unpackName(const uint8_t in[8]);

}  // namespace rvc
