#include "wheel_codec.h"

#include <cmath>

#include "codec.h"

namespace rvc {

namespace {

constexpr uint8_t U8_MAX_VALID = 0xFA;
constexpr uint8_t U8_ERR = 0xFE;
constexpr uint8_t U8_NA = 0xFF;
// Record 01h byte 5 when faulted: level, short and settling n/a (11b), sensor fault 01b.
constexpr uint8_t kFaultFlags = 0xDF;

uint8_t countByte(uint32_t n) { return n <= U8_MAX_VALID ? static_cast<uint8_t>(n) : U8_ERR; }

// 0.01 in -> 0.1 in, rounded.
uint8_t liftByte(uint32_t hundredths) { return countByte((hundredths + 5) / 10); }

uint8_t flag(bool on, int shift) { return static_cast<uint8_t>((on ? 1 : 0) << shift); }

void packPerWheel(uint8_t record, const WheelStatusMsg& m, const uint32_t values[4],
                  uint8_t (*encode)(uint32_t), uint8_t out[8]) {
  out[0] = record;
  for (int w = 0; w < level::kWheelCount; ++w) out[1 + w] = m.valid ? encode(values[w]) : U8_NA;
  out[5] = out[6] = out[7] = U8_NA;
}

}  // namespace

uint16_t angleTenths(float deg) {
  double v = std::fabs(static_cast<double>(deg)) * 10.0;
  if (!(v < 64255.0)) return 0xFAFF;
  return static_cast<uint16_t>(std::lround(v));
}

void packWheelAttitude(const WheelStatusMsg& m, uint8_t out[8]) {
  out[0] = wheelrec::ATTITUDE;
  if (!m.valid) {
    put16(out + 1, U16_NA);
    put16(out + 3, U16_NA);
    out[5] = kFaultFlags;
    out[6] = U8_NA;
    out[7] = U8_NA;
    return;
  }
  uint16_t p = angleTenths(m.pitchDeg);
  uint16_t r = angleTenths(m.rollDeg);
  put16(out + 1, p);
  put16(out + 3, r);
  out[5] = flag(m.plan.level, 0) | flag(m.plan.shortOnBlocks, 2) | flag(m.moving, 6);  // bits 4-5: no fault
  // A direction only when the rounded magnitude is non-zero, so the pair always agrees.
  out[6] = flag(p && m.pitchDeg > 0, 0) | flag(p && m.pitchDeg < 0, 2) | flag(r && m.rollDeg > 0, 4) |
           flag(r && m.rollDeg < 0, 6);
  out[7] = countByte(m.plan.totalLayers);
}

void packWheelLayers(const WheelStatusMsg& m, uint8_t out[8]) {
  packPerWheel(wheelrec::LAYERS, m, m.plan.layers, countByte, out);
  // Byte 5 bits 0-1: tolerance auto 01b / fixed 00b, 11b (n/a) when not valid; other bits 1.
  if (m.valid) out[5] = static_cast<uint8_t>(0xFC | (m.toleranceAuto ? 1 : 0));
}

void packWheelLift(const WheelStatusMsg& m, uint8_t out[8]) {
  packPerWheel(wheelrec::LIFT, m, m.plan.liftHundredths, liftByte, out);
}

void packWheelConfig(const level::BlockConfig& c, uint8_t out[8]) {
  out[0] = wheelrec::CONFIG;
  put16(out + 1, c.wheelbaseTenthIn);
  put16(out + 3, c.trackTenthIn);
  out[5] = c.blockHeight;
  out[6] = c.blocksCarried;
  uint32_t tol = level::effectiveTolerance(c);  // auto shows as the value it works out to
  out[7] = static_cast<uint8_t>(tol < 0xFA ? tol : 0xFA);
}

}  // namespace rvc
