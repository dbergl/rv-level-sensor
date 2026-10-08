#pragma once
#include <cstdint>

namespace level {

// Tracks consecutive read failures and paces re-initialisation attempts.
class SensorHealth {
 public:
  explicit SensorHealth(uint8_t failLimit = 3, uint32_t retryMs = 2000);
  void onReadOk();
  void onReadFail(uint32_t nowMs);
  void markFaulted(uint32_t nowMs);
  bool faulted() const { return faulted_; }
  // True at most once per retry interval while faulted.
  bool shouldRetryInit(uint32_t nowMs);

 private:
  uint8_t failLimit_;
  uint32_t retryMs_;
  uint8_t fails_ = 0;
  bool faulted_ = false;
  uint32_t lastRetryMs_ = 0;
};

}  // namespace level
