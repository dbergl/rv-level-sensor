#pragma once
#include <cstdint>

#include "rvc_types.h"

namespace rvc {

// Dynamic source-address claiming (spec 3.3). Lower NAME value = higher priority.
class AddressClaimer {
 public:
  static constexpr uint32_t kClaimWaitMs = 250;
  static constexpr uint32_t kFailedRetryMs = 30000;

  AddressClaimer(uint64_t name, uint8_t rangeHigh, uint8_t rangeLow);

  // preferredSa is used if it lies within [rangeLow, rangeHigh], else rangeHigh.
  void begin(uint8_t preferredSa, uint32_t nowMs);
  void tick(uint32_t nowMs, ITransmitter& tx);
  // An ADDRESS_CLAIM (EExxh) received from `sa`.
  void onAddressClaim(uint8_t sa, uint64_t theirName, uint32_t nowMs, ITransmitter& tx);
  // A request for ADDRESS_CLAIM addressed to `dest` (ours or FFh).
  void onRequestForClaim(uint8_t dest, ITransmitter& tx);

  bool claimed() const { return state_ == State::Claimed; }
  // Claimed and the 250 ms post-claim quiet period has elapsed (spec 4.3.1.2, 00D-R04).
  bool ready(uint32_t nowMs) const { return claimed() && nowMs - claimedMs_ >= kClaimWaitMs; }
  bool failed() const { return state_ == State::Failed; }
  uint8_t address() const { return claimed() ? candidate_ : ADDR_NULL; }
  uint64_t name() const { return name_; }

 private:
  enum class State { Idle, NeedRequest, Waiting, Claimed, Failed };

  bool sendRequest(ITransmitter& tx);
  bool sendClaim(uint8_t sa, ITransmitter& tx);
  void advance(uint32_t nowMs, ITransmitter& tx);
  void enterClaimed(uint32_t nowMs);

  uint64_t name_;
  uint8_t rangeHigh_;
  uint8_t rangeLow_;
  State state_ = State::Idle;
  uint8_t candidate_ = 0;
  uint16_t tried_ = 0;
  uint32_t stateMs_ = 0;
  uint32_t claimedMs_ = 0;
  uint32_t lastReclaimMs_ = 0;
};

}  // namespace rvc
