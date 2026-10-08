#include "axis_config.h"

#include <cmath>

namespace rvc {

bool isValidAxisSettings(const AxisSettings& s) {
  if (!std::isfinite(s.outDeg) || !std::isfinite(s.withinDeg) || !std::isfinite(s.zeroDeg)) return false;
  return s.withinDeg >= 0.0f && s.withinDeg <= s.outDeg && s.outDeg <= kMaxThresholdDeg &&
         std::fabs(s.zeroDeg) <= kMaxZeroDeg;
}

ConfigResult applyAxisConfigCommand(const AxisConfigMsg& cmd, const AxisSettings& current,
                                    uint8_t ourInstance, const AxisReading& reading) {
  ConfigResult r{ConfigOutcome::Ignored, current, false, ack::OK};
  if (cmd.instance != 0 && cmd.instance != ourInstance) return r;

  auto reject = [&r](uint8_t code) {
    r.outcome = ConfigOutcome::Rejected;
    r.next = AxisSettings{};
    r.sendAck = true;
    r.ackCode = code;
    return r;
  };

  if (cmd.setZero == 2) return reject(ack::BAD_FORMAT);

  AxisSettings next = current;
  if (cmd.outRaw != U16_NA) next.outDeg = decodeAngle(cmd.outRaw);
  if (cmd.withinRaw != U16_NA) next.withinDeg = decodeAngle(cmd.withinRaw);
  if (!isValidAxisSettings(next)) return reject(ack::OUT_OF_RANGE);

  bool setZero = cmd.setZero == 1;
  if (setZero) {
    if (!reading.valid || reading.moving) return reject(ack::CONDITIONS);
    next.zeroDeg = reading.rawDeg;
  }

  r.outcome = ConfigOutcome::Applied;
  r.next = next;
  r.sendAck = setZero;
  r.ackCode = ack::OK;
  return r;
}

}  // namespace rvc
