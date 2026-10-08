#include "sensor_health.h"

namespace level {

SensorHealth::SensorHealth(uint8_t failLimit, uint32_t retryMs)
    : failLimit_(failLimit), retryMs_(retryMs) {}

void SensorHealth::onReadOk() {
  fails_ = 0;
  faulted_ = false;
}

void SensorHealth::onReadFail(uint32_t nowMs) {
  if (fails_ < failLimit_) ++fails_;
  if (fails_ >= failLimit_ && !faulted_) markFaulted(nowMs);
}

void SensorHealth::markFaulted(uint32_t nowMs) {
  fails_ = failLimit_;
  faulted_ = true;
  lastRetryMs_ = nowMs;
}

bool SensorHealth::shouldRetryInit(uint32_t nowMs) {
  if (!faulted_ || nowMs - lastRetryMs_ < retryMs_) return false;
  lastRetryMs_ = nowMs;
  return true;
}

}  // namespace level
