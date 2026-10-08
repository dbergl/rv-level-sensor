#include "attitude.h"

#include <cmath>

namespace level {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;

Vec3 unit(Axis a) {
  int8_t v = static_cast<int8_t>(a);
  float s = v < 0 ? -1.0f : 1.0f;
  switch (v < 0 ? -v : v) {
    case 1: return {s, 0, 0};
    case 2: return {0, s, 0};
    default: return {0, 0, s};
  }
}

Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float magnitude(const Vec3& v) { return std::sqrt(dot(v, v)); }

}  // namespace

Vec3 mapToCoach(const Vec3& sensor, const AxisMap& map) {
  Vec3 f = unit(map.forward);
  Vec3 d = unit(map.driver);
  Vec3 u = cross(f, d);
  return {dot(sensor, f), dot(sensor, d), dot(sensor, u)};
}

float pitchDeg(const Vec3& c) { return std::atan2(c.x, std::sqrt(c.y * c.y + c.z * c.z)) * kRadToDeg; }

float rollDeg(const Vec3& c) { return std::atan2(c.y, std::sqrt(c.x * c.x + c.z * c.z)) * kRadToDeg; }

GyroBias::GyroBias(uint16_t samples, float trackBelowDps, float trackGain, float maxSpreadDps)
    : samples_(samples), trackBelowDps_(trackBelowDps), trackGain_(trackGain), maxSpreadDps_(maxSpreadDps) {}

Vec3 GyroBias::correct(const Vec3& raw) {
  if (!ready_) {
    if (count_ == 0) {
      sum_ = {};
      min_ = max_ = raw;
    }
    sum_.x += raw.x;
    sum_.y += raw.y;
    sum_.z += raw.z;
    min_ = {std::fmin(min_.x, raw.x), std::fmin(min_.y, raw.y), std::fmin(min_.z, raw.z)};
    max_ = {std::fmax(max_.x, raw.x), std::fmax(max_.y, raw.y), std::fmax(max_.z, raw.z)};
    if (++count_ >= samples_) {
      bool still = max_.x - min_.x <= maxSpreadDps_ && max_.y - min_.y <= maxSpreadDps_ &&
                   max_.z - min_.z <= maxSpreadDps_;
      if (still) {
        bias_ = {sum_.x / count_, sum_.y / count_, sum_.z / count_};
        ready_ = true;
      }
      count_ = 0;
    }
    return raw;
  }
  Vec3 c{raw.x - bias_.x, raw.y - bias_.y, raw.z - bias_.z};
  if (magnitude(c) < trackBelowDps_) {
    bias_.x += trackGain_ * c.x;
    bias_.y += trackGain_ * c.y;
    bias_.z += trackGain_ * c.z;
  }
  return c;
}

AttitudeFilter::AttitudeFilter(float tauS, float motionDps, uint32_t motionWindowMs)
    : tauS_(tauS), motionDps_(motionDps), motionWindowMs_(motionWindowMs) {}

void AttitudeFilter::update(const Vec3& a, const Vec3& gyroDps, uint32_t nowMs) {
  if (!seeded_) {
    g_ = a;
    seeded_ = true;
  } else {
    float dt = static_cast<float>(nowMs - lastMs_) / 1000.0f;
    float alpha = dt / (tauS_ + dt);
    g_.x += alpha * (a.x - g_.x);
    g_.y += alpha * (a.y - g_.y);
    g_.z += alpha * (a.z - g_.z);
  }
  lastMs_ = nowMs;
  if (magnitude(gyroDps) > motionDps_) {
    hadMotion_ = true;
    lastMotionMs_ = nowMs;
  }
}

void AttitudeFilter::reset() {
  seeded_ = false;
  hadMotion_ = false;
}

float AttitudeFilter::pitchDeg() const { return level::pitchDeg(g_); }

float AttitudeFilter::rollDeg() const { return level::rollDeg(g_); }

bool AttitudeFilter::moving(uint32_t nowMs) const {
  return hadMotion_ && (nowMs - lastMotionMs_) < motionWindowMs_;
}

}  // namespace level
