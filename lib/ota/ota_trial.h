#pragma once
#include <cstdint>

namespace ota {

enum class TrialAction : uint8_t { None, Wait, Confirm, RollBack };

constexpr uint32_t kTrialMinUptimeMs = 30000;
constexpr uint32_t kTrialDeadlineMs = 60000;

// What to do with an image that booted on trial (ESP-IDF PENDING_VERIFY). It is kept once it has
// claimed its RV-C address, produced a valid IMU reading and run for 30 s; otherwise it is rolled
// back at 60 s. Not on trial: nothing to do.
TrialAction decideTrial(bool onTrial, uint32_t uptimeMs, bool claimed, bool imuValidSeen);

}  // namespace ota
