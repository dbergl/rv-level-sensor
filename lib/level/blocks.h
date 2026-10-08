#pragma once
#include <cstdint>

namespace level {

// Block-leveling configuration, kept in the units LEVEL_WHEEL_STATUS record 04h carries.
struct BlockConfig {
  uint16_t wheelbaseTenthIn = 1590;   // 0.1 in: 159.0 in
  uint16_t trackTenthIn = 685;        // 0.1 in: 68.5 in
  uint8_t blockHeight = 20;           // 0.05 in: 1.00 in
  uint8_t blocksCarried = 10;
  uint8_t toleranceHundredthIn = 0xFE;  // 0.01 in, or FEh = auto: half the block height

  bool operator==(const BlockConfig& o) const {
    return wheelbaseTenthIn == o.wheelbaseTenthIn && trackTenthIn == o.trackTenthIn &&
           blockHeight == o.blockHeight && blocksCarried == o.blocksCarried &&
           toleranceHundredthIn == o.toleranceHundredthIn;
  }
  bool operator!=(const BlockConfig& o) const { return !(*this == o); }
};

// Valid ranges, raw units. u8 values stop at FAh (RV-C: FEh is "out of range").
constexpr uint16_t kWheelbaseMin = 800, kWheelbaseMax = 3000;  // 80-300 in
constexpr uint16_t kTrackMin = 400, kTrackMax = 1100;          // 40-110 in
constexpr uint8_t kBlockHeightMin = 1, kBlockHeightMax = 250;  // 0.05-12.50 in
constexpr uint8_t kBlocksCarriedMax = 250;
constexpr uint8_t kToleranceMax = 200;  // 2.00 in
constexpr uint8_t kToleranceAuto = 0xFE;  // tolerance follows the block height (the default)

bool isValidBlockConfig(const BlockConfig& c);

// The tolerance in force, 0.01 in: the fixed value, or half the block height when auto.
uint32_t effectiveTolerance(const BlockConfig& c);

enum Wheel : uint8_t { kFrontLeft = 0, kFrontRight, kRearLeft, kRearRight, kWheelCount };

// Lifts above this (or not finite, e.g. at 90 deg) saturate here: 10 000 in, in 0.01 in.
constexpr uint32_t kMaxLiftHundredths = 1000000;

struct BlockPlan {
  uint32_t liftHundredths[kWheelCount] = {};  // 0.01 in
  uint32_t layers[kWheelCount] = {};
  uint32_t totalLayers = 0;
  bool level = true;           // every wheel needs 0 layers
  bool shortOnBlocks = false;  // totalLayers > blocksCarried
};

// Layers under a wheel that must rise `liftHundredths`: 0 within the tolerance, otherwise the
// nearest whole number of blocks, exactly half rounding down, so a block is only added when it
// brings the wheel closer. All in 0.01 in, so exact multiples stay exact.
uint32_t layersFor(uint32_t liftHundredths, const BlockConfig& c);

// Zeroed angles: +pitch nose up, +roll driver side up. Wheels can only be raised, so the
// highest wheel is the reference and every other wheel comes up to it.
BlockPlan planBlocks(float pitchDeg, float rollDeg, const BlockConfig& c);

}  // namespace level
