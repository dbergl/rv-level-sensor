#include "level_logic.h"

#include <cmath>

namespace level {

int8_t LevelLogic::AxisState::step(float angleDeg) {
  if (angleDeg < -out) {
    state = -1;
  } else if (angleDeg > out) {
    state = 1;
  } else if (std::fabs(angleDeg) <= within) {
    state = 0;
  }
  return state;
}

LevelLogic::LevelLogic(float outDeg, float withinDeg)
    : pitch_{outDeg, withinDeg, 0}, roll_{outDeg, withinDeg, 0} {}

void LevelLogic::setPitchThresholds(float outDeg, float withinDeg) {
  pitch_.out = outDeg;
  pitch_.within = withinDeg;
}

void LevelLogic::setRollThresholds(float outDeg, float withinDeg) {
  roll_.out = outDeg;
  roll_.within = withinDeg;
}

LevelBits LevelLogic::update(float pitchDeg, float rollDeg) {
  int8_t p = pitch_.step(pitchDeg);
  int8_t r = roll_.step(rollDeg);
  LevelBits b;
  b.frontLow = p < 0;      // nose low
  b.rearLow = p > 0;       // nose high
  b.driverLow = r < 0;     // driver side low
  b.passengerLow = r > 0;  // driver side high
  return b;
}

void LevelLogic::reset() {
  pitch_.state = 0;
  roll_.state = 0;
}

}  // namespace level
