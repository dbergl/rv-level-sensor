#pragma once
#include <cstdint>

namespace level {

// Milliseconds until a task last run at `lastMs` is due again, 0 if due now or overdue.
// Unsigned arithmetic keeps it correct across the millis() wrap.
inline uint32_t msUntilDue(uint32_t nowMs, uint32_t lastMs, uint32_t periodMs) {
  uint32_t elapsed = nowMs - lastMs;
  return elapsed >= periodMs ? 0 : periodMs - elapsed;
}

}  // namespace level
