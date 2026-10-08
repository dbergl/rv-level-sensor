#pragma once
#include <cstdint>

#include "blocks.h"

namespace rvc {

// Record numbers in byte 0 of LEVEL_WHEEL_STATUS (1EFBFh).
namespace wheelrec {
constexpr uint8_t ATTITUDE = 0x01;
constexpr uint8_t LAYERS = 0x02;
constexpr uint8_t LIFT = 0x03;
constexpr uint8_t CONFIG = 0x04;
}  // namespace wheelrec

struct WheelStatusMsg {
  bool valid;      // false: sensor fault, every value not available (request §3.5)
  bool moving;     // "settling" flag
  float pitchDeg;  // zeroed, + nose up
  float rollDeg;   // zeroed, + driver side up
  level::BlockPlan plan;
  bool toleranceAuto = false;  // record 02h byte 5 bits 0-1
};

// |deg| in 0.1 deg, rounded; FAFFh (largest valid u16) when too large or not finite.
uint16_t angleTenths(float deg);

void packWheelAttitude(const WheelStatusMsg& m, uint8_t out[8]);   // record 01h
void packWheelLayers(const WheelStatusMsg& m, uint8_t out[8]);     // record 02h
void packWheelLift(const WheelStatusMsg& m, uint8_t out[8]);       // record 03h
void packWheelConfig(const level::BlockConfig& c, uint8_t out[8]);  // record 04h

}  // namespace rvc
