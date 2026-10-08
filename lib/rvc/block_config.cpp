#include "block_config.h"

#include <algorithm>

#include "codec.h"

namespace rvc {
namespace {

constexpr int kHeightStep = 5;  // 0.25 in, in 0.05 in
constexpr uint32_t kTolStep = 5;  // 0.05 in, in 0.01 in
constexpr int kGeometryStep = 5;  // 0.5 in, in 0.1 in

uint16_t stepUp(uint16_t v, uint16_t max) { return static_cast<uint16_t>(std::min<int>(v + kGeometryStep, max)); }
uint16_t stepDown(uint16_t v, uint16_t min) { return static_cast<uint16_t>(std::max<int>(v - kGeometryStep, min)); }

}  // namespace

BlockCmdResult applyBlockConfigCommand(const uint8_t* d, uint8_t len, const level::BlockConfig& current) {
  if (len < 3 || d[0] != blockcmd::SIG0 || d[1] != blockcmd::SIG1) return {BlockCmdOutcome::Ignored, current};
  const BlockCmdResult rejected{BlockCmdOutcome::Rejected, current};
  if (len < 8) return rejected;

  level::BlockConfig next = current;
  switch (d[2]) {
    case blockcmd::SET_GEOMETRY:
      if (get16(d + 3) != U16_NA) next.wheelbaseTenthIn = get16(d + 3);
      if (get16(d + 5) != U16_NA) next.trackTenthIn = get16(d + 5);
      break;
    case blockcmd::SET_BLOCKS:
      if (d[3] != 0xFF) next.blockHeight = d[3];
      if (d[4] != 0xFF) next.blocksCarried = d[4];
      if (d[5] != 0xFF) next.toleranceHundredthIn = d[5];
      break;
    case blockcmd::CARRIED_UP:
      if (next.blocksCarried < level::kBlocksCarriedMax) ++next.blocksCarried;
      break;
    case blockcmd::CARRIED_DOWN:
      if (next.blocksCarried > 0) --next.blocksCarried;
      break;
    case blockcmd::RESTORE_DEFAULTS:
      next = level::BlockConfig{};
      break;
    case blockcmd::HEIGHT_UP:
      next.blockHeight = next.blockHeight + kHeightStep < level::kBlockHeightMax ? next.blockHeight + kHeightStep
                                                                                  : level::kBlockHeightMax;
      break;
    case blockcmd::HEIGHT_DOWN:
      if (next.blockHeight > kHeightStep) next.blockHeight = std::max<int>(next.blockHeight - kHeightStep, kHeightStep);
      break;
    case blockcmd::TOL_UP:
      next.toleranceHundredthIn = std::min<uint32_t>(level::effectiveTolerance(next) + kTolStep, level::kToleranceMax);
      break;
    case blockcmd::TOL_DOWN: {
      uint32_t tol = std::min<uint32_t>(level::effectiveTolerance(next), level::kToleranceMax);
      next.toleranceHundredthIn = tol > kTolStep ? tol - kTolStep : 0;
      break;
    }
    case blockcmd::TOL_AUTO:
      next.toleranceHundredthIn = level::kToleranceAuto;
      break;
    case blockcmd::WHEELBASE_UP:
      next.wheelbaseTenthIn = stepUp(next.wheelbaseTenthIn, level::kWheelbaseMax);
      break;
    case blockcmd::WHEELBASE_DOWN:
      next.wheelbaseTenthIn = stepDown(next.wheelbaseTenthIn, level::kWheelbaseMin);
      break;
    case blockcmd::TRACK_UP:
      next.trackTenthIn = stepUp(next.trackTenthIn, level::kTrackMax);
      break;
    case blockcmd::TRACK_DOWN:
      next.trackTenthIn = stepDown(next.trackTenthIn, level::kTrackMin);
      break;
    default:
      return rejected;
  }
  if (!level::isValidBlockConfig(next)) return rejected;
  return {BlockCmdOutcome::Applied, next};
}

}  // namespace rvc
