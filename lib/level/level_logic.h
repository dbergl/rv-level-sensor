#pragma once
#include <cstdint>

namespace level {

struct LevelBits {
  bool driverLow = false;
  bool frontLow = false;
  bool passengerLow = false;
  bool rearLow = false;

  bool any() const { return driverLow || frontLow || passengerLow || rearLow; }
  bool operator==(const LevelBits& o) const {
    return driverLow == o.driverLow && frontLow == o.frontLow &&
           passengerLow == o.passengerLow && rearLow == o.rearLow;
  }
  bool operator!=(const LevelBits& o) const { return !(*this == o); }
};

// Hysteresis per axis: a side becomes "below level" beyond the out-of-level
// threshold and returns to level only once |angle| <= the within-level threshold.
class LevelLogic {
 public:
  LevelLogic(float outDeg = 1.0f, float withinDeg = 0.5f);
  void setPitchThresholds(float outDeg, float withinDeg);
  void setRollThresholds(float outDeg, float withinDeg);
  // Pitch: + nose high. Roll: + driver side high.
  LevelBits update(float pitchDeg, float rollDeg);
  void reset();

 private:
  struct AxisState {
    float out;
    float within;
    int8_t state;  // -1 negative side low, 0 level, +1 positive side low
    int8_t step(float angleDeg);
  };
  AxisState pitch_;
  AxisState roll_;
};

}  // namespace level
