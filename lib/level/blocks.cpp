#include "blocks.h"

#include <cmath>

namespace level {

bool isValidBlockConfig(const BlockConfig& c) {
  return c.wheelbaseTenthIn >= kWheelbaseMin && c.wheelbaseTenthIn <= kWheelbaseMax &&
         c.trackTenthIn >= kTrackMin && c.trackTenthIn <= kTrackMax && c.blockHeight >= kBlockHeightMin &&
         c.blockHeight <= kBlockHeightMax && c.blocksCarried <= kBlocksCarriedMax &&
         (c.toleranceHundredthIn <= kToleranceMax || c.toleranceHundredthIn == kToleranceAuto);
}

uint32_t effectiveTolerance(const BlockConfig& c) {
  return c.toleranceHundredthIn == kToleranceAuto ? c.blockHeight * 5u / 2u : c.toleranceHundredthIn;
}

uint32_t layersFor(uint32_t liftHundredths, const BlockConfig& c) {
  if (liftHundredths <= effectiveTolerance(c)) return 0;
  uint32_t step = (c.blockHeight ? c.blockHeight : 1u) * 5u;  // 0.05 in = 5 hundredths
  return (2 * liftHundredths + step - 1) / (2 * step);  // nearest; exactly half rounds down
}

BlockPlan planBlocks(float pitchDeg, float rollDeg, const BlockConfig& c) {
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  // Tenths of an inch / 20 = half the length in inches.
  double a = c.wheelbaseTenthIn / 20.0 * std::tan(pitchDeg * kDegToRad);
  double b = c.trackTenthIn / 20.0 * std::tan(rollDeg * kDegToRad);
  const double h[kWheelCount] = {a + b, a - b, b - a, -a - b};  // FL, FR, RL, RR
  double top = h[0];
  for (double x : h) top = std::fmax(top, x);

  BlockPlan p;
  for (int w = 0; w < kWheelCount; ++w) {
    double lift = (top - h[w]) * 100.0;
    // Written so NaN also takes the saturated branch.
    p.liftHundredths[w] = lift < kMaxLiftHundredths ? static_cast<uint32_t>(std::lround(lift)) : kMaxLiftHundredths;
    p.layers[w] = layersFor(p.liftHundredths[w], c);
    p.totalLayers += p.layers[w];
  }
  p.level = p.totalLayers == 0;
  p.shortOnBlocks = p.totalLayers > c.blocksCarried;
  return p;
}

}  // namespace level
