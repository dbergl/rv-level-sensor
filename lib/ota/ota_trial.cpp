#include "ota_trial.h"

namespace ota {

TrialAction decideTrial(bool onTrial, uint32_t uptimeMs, bool claimed, bool imuValidSeen) {
  if (!onTrial) return TrialAction::None;
  if (claimed && imuValidSeen && uptimeMs >= kTrialMinUptimeMs) return TrialAction::Confirm;
  if (uptimeMs >= kTrialDeadlineMs) return TrialAction::RollBack;
  return TrialAction::Wait;
}

}  // namespace ota
