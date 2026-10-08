#pragma once
#include <cstdint>

namespace level {

struct Vec3 {
  float x = 0;
  float y = 0;
  float z = 0;
};

// Signed sensor axis. Magnitude selects X/Y/Z, sign selects direction.
enum class Axis : int8_t { PosX = 1, NegX = -1, PosY = 2, NegY = -2, PosZ = 3, NegZ = -3 };

struct AxisMap {
  Axis forward;  // sensor axis pointing toward the front of the coach
  Axis driver;   // sensor axis pointing toward the driver side
};

constexpr bool isValidAxisMap(Axis forward, Axis driver) {
  return static_cast<int8_t>(forward) != static_cast<int8_t>(driver) &&
         static_cast<int8_t>(forward) != -static_cast<int8_t>(driver);
}

// Returns the vector in coach coordinates: x = forward, y = driver side, z = up (forward x driver).
Vec3 mapToCoach(const Vec3& sensor, const AxisMap& map);

// From an accelerometer reading at rest (specific force, points up), in coach coordinates.
float pitchDeg(const Vec3& coach);  // + nose high
float rollDeg(const Vec3& coach);   // + driver side high

// Averages the first N gyro samples as the bias, then tracks slow drift while still.
// A capture window whose per-axis spread exceeds maxSpreadDps (the sensor moved) is discarded and restarted.
class GyroBias {
 public:
  explicit GyroBias(uint16_t samples = 200, float trackBelowDps = 0.5f, float trackGain = 0.001f,
                    float maxSpreadDps = 0.3f);
  Vec3 correct(const Vec3& rawDps);
  bool ready() const { return ready_; }

 private:
  uint16_t samples_;
  float trackBelowDps_;
  float trackGain_;
  float maxSpreadDps_;
  uint16_t count_ = 0;
  Vec3 sum_;
  Vec3 min_;
  Vec3 max_;
  Vec3 bias_;
  bool ready_ = false;
};

// Low-pass filters the gravity vector and detects motion from the gyro.
class AttitudeFilter {
 public:
  AttitudeFilter(float tauS = 1.0f, float motionDps = 0.5f, uint32_t motionWindowMs = 1000);
  void update(const Vec3& accelCoach, const Vec3& gyroDps, uint32_t nowMs);
  void reset();
  bool seeded() const { return seeded_; }
  float pitchDeg() const;
  float rollDeg() const;
  bool moving(uint32_t nowMs) const;

 private:
  float tauS_;
  float motionDps_;
  uint32_t motionWindowMs_;
  bool seeded_ = false;
  Vec3 g_;
  uint32_t lastMs_ = 0;
  bool hadMotion_ = false;
  uint32_t lastMotionMs_ = 0;
};

}  // namespace level
