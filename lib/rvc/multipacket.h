#pragma once
#include <cstdint>

#include "rvc_types.h"

namespace rvc {

// Broadcast long-message transmitter (spec 3.2.7): one INITIAL_PACKET then
// DATA_PACKETs, each frame at least 50 ms after the previous one.
class MultiPacketTx {
 public:
  static constexpr uint16_t kMaxLen = 64;
  static constexpr uint32_t kGapMs = 50;

  bool start(uint32_t dgn, const uint8_t* data, uint16_t len);
  bool busy() const { return busy_; }
  void cancel() { busy_ = false; }
  // Sends the next frame if it is due. A failed send is retried on the next poll.
  void poll(uint32_t nowMs, uint8_t sa, ITransmitter& tx);

 private:
  uint8_t buf_[kMaxLen] = {};
  uint16_t len_ = 0;
  uint8_t packets_ = 0;
  uint8_t next_ = 0;  // 0 = initial packet, then 1..packets_
  uint32_t dgn_ = 0;
  uint32_t lastMs_ = 0;
  bool busy_ = false;
};

}  // namespace rvc
