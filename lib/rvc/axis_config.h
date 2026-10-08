#pragma once
#include <cstdint>

#include "blocks.h"
#include "codec.h"

namespace rvc {

struct AxisSettings {
  float outDeg = 1.0f;     // out-of-level threshold
  float withinDeg = 0.5f;  // within-level threshold
  float zeroDeg = 0.0f;    // relative zero, subtracted from the measured angle
};

struct Settings {
  AxisSettings pitch;
  AxisSettings roll;
  level::BlockConfig blocks;  // LEVEL_CONFIG_COMMAND (block_config.h)
};

constexpr float kMaxThresholdDeg = 45.0f;
constexpr float kMaxZeroDeg = 90.0f;

// Codes from spec Table 7.5.
namespace ack {
constexpr uint8_t OK = 0;
constexpr uint8_t NAK = 1;
constexpr uint8_t CONDITIONS = 3;
constexpr uint8_t BAD_FORMAT = 4;
constexpr uint8_t OUT_OF_RANGE = 5;
}  // namespace ack

// 0 <= within <= out <= 45 deg and |zero| <= 90 deg, all finite.
bool isValidAxisSettings(const AxisSettings& s);

struct AxisReading {
  bool valid;
  bool moving;
  float rawDeg;  // filtered angle before the relative zero is applied
};

enum class ConfigOutcome { Ignored, Applied, Rejected };

struct ConfigResult {
  ConfigOutcome outcome;
  AxisSettings next;  // settings to apply when outcome == Applied
  bool sendAck;
  uint8_t ackCode;
};

// Pure decision logic for LEVELING_SENSOR_{PITCH,ROLL}_CONFIG_COMMAND (spec 6.13.8).
ConfigResult applyAxisConfigCommand(const AxisConfigMsg& cmd, const AxisSettings& current,
                                    uint8_t ourInstance, const AxisReading& reading);

}  // namespace rvc
