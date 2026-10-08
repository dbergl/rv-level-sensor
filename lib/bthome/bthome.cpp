#include "bthome.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace bthome {

namespace {
constexpr uint8_t kDeviceInfo = 0x40;  // v2, unencrypted, regular interval
constexpr uint8_t kPacketId = 0x00;
constexpr uint8_t kGenericBool = 0x0F;
constexpr uint8_t kMoving = 0x22;
constexpr uint8_t kProblem = 0x26;
constexpr uint8_t kRotation = 0x3F;
}  // namespace

int16_t encodeRotation(float deg) {
  long v = std::lround(deg * 10.0f);
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<int16_t>(v);
}

size_t Encoder::encode(const Reading& r, uint8_t* out, size_t cap, bool newPacket) {
  // Content after the packet ID, built first so it can be compared with the last one.
  uint8_t body[kMaxBody - 3];
  size_t n = 0;
  body[n++] = kGenericBool;
  body[n++] = (r.valid && r.level) ? 1 : 0;
  body[n++] = kMoving;
  body[n++] = r.moving ? 1 : 0;
  body[n++] = kProblem;
  body[n++] = r.problem ? 1 : 0;
  if (r.valid) {
    for (float deg : {r.pitchDeg, r.rollDeg}) {
      uint16_t v = static_cast<uint16_t>(encodeRotation(deg));
      body[n++] = kRotation;
      body[n++] = static_cast<uint8_t>(v & 0xFF);
      body[n++] = static_cast<uint8_t>(v >> 8);
    }
  }
  size_t total = 3 + n;
  if (cap < total) return 0;

  if (started_ && (newPacket || n != lastLen_ || std::memcmp(body, last_, n) != 0)) ++packetId_;
  started_ = true;
  std::memcpy(last_, body, n);
  lastLen_ = n;

  out[0] = kDeviceInfo;
  out[1] = kPacketId;
  out[2] = packetId_;
  std::memcpy(out + 3, body, n);
  return total;
}

bool AdvertSchedule::refreshDue(uint32_t nowMs) {
  if (refreshed_ && nowMs - lastRefreshMs_ < refreshMs_) return false;
  refreshed_ = true;
  lastRefreshMs_ = nowMs;
  return true;
}

bool AdvertSchedule::shouldSend(bool changed, uint32_t nowMs) {
  if (sent_ && !changed && nowMs - lastSendMs_ < reapplyMs_) return false;
  sent_ = true;
  lastSendMs_ = nowMs;
  return true;
}

}  // namespace bthome
