#pragma once
#include <cstddef>
#include <cstdint>

// BTHome v2 (https://bthome.io/format/) service-data encoder for the level sensor.
namespace bthome {

constexpr uint16_t kServiceUuid = 0xFCD2;
constexpr size_t kMaxBody = 15;

struct Reading {
  bool valid;      // angles usable (not faulted and filter seeded)
  bool level;      // no side below level
  bool moving;
  bool problem;    // IMU fault
  float pitchDeg;  // + nose up
  float rollDeg;   // + driver side up
};

// Rotation object: 0.1 deg per bit, rounded to nearest (halves away from zero), clamped.
int16_t encodeRotation(float deg);

class Encoder {
 public:
  explicit Encoder(uint8_t initialPacketId = 0) : packetId_(initialPacketId) {}
  // Writes the service-data body (everything after the UUID) and returns its length,
  // or 0 if `cap` is too small. The packet ID changes only when the content does, or when
  // `newPacket` asks for a fresh one (after the first call).
  size_t encode(const Reading& r, uint8_t* out, size_t cap, bool newPacket = false);

 private:
  uint8_t packetId_;
  bool started_ = false;
  uint8_t last_[kMaxBody] = {};
  size_t lastLen_ = 0;
};

// When the radio wrapper re-encodes (at most every refreshMs) and when it re-sends the
// advert data: on a content change, and every reapplyMs regardless, so the advert recovers
// if a BLE host reset clears the controller's copy.
class AdvertSchedule {
 public:
  AdvertSchedule(uint32_t refreshMs, uint32_t reapplyMs) : refreshMs_(refreshMs), reapplyMs_(reapplyMs) {}
  bool refreshDue(uint32_t nowMs);
  bool shouldSend(bool changed, uint32_t nowMs);

 private:
  uint32_t refreshMs_;
  uint32_t reapplyMs_;
  bool refreshed_ = false;
  uint32_t lastRefreshMs_ = 0;
  bool sent_ = false;
  uint32_t lastSendMs_ = 0;
};

}  // namespace bthome
