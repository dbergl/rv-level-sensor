#include "address_claim.h"

#include "codec.h"
#include "dgn.h"
#include "frame.h"

namespace rvc {

AddressClaimer::AddressClaimer(uint64_t name, uint8_t rangeHigh, uint8_t rangeLow)
    : name_(name), rangeHigh_(rangeHigh), rangeLow_(rangeLow) {}

void AddressClaimer::begin(uint8_t preferredSa, uint32_t nowMs) {
  candidate_ = (preferredSa >= rangeLow_ && preferredSa <= rangeHigh_) ? preferredSa : rangeHigh_;
  tried_ = 0;
  state_ = State::NeedRequest;
  stateMs_ = nowMs;
}

bool AddressClaimer::sendRequest(ITransmitter& tx) {
  CanFrame f;
  f.id = makeId(prio::NORMAL, dgn::REQUEST | candidate_, ADDR_NULL);
  packRequest(dgn::ADDRESS_CLAIM, f.data);
  return tx.send(f);
}

bool AddressClaimer::sendClaim(uint8_t sa, ITransmitter& tx) {
  CanFrame f;
  f.id = makeId(prio::NORMAL, dgn::ADDRESS_CLAIM, sa);  // RV-C uses EE00h (spec 4.3.1.2)
  packName(name_, f.data);
  return tx.send(f);
}

void AddressClaimer::advance(uint32_t nowMs, ITransmitter& tx) {
  stateMs_ = nowMs;
  if (++tried_ >= 254) {
    state_ = State::Failed;
    sendClaim(ADDR_NULL, tx);  // "cannot claim"
    return;
  }
  candidate_ = candidate_ == 0 ? 253 : static_cast<uint8_t>(candidate_ - 1);
  state_ = State::NeedRequest;
}

void AddressClaimer::enterClaimed(uint32_t nowMs) {
  state_ = State::Claimed;
  claimedMs_ = nowMs;
  lastReclaimMs_ = nowMs;
}

void AddressClaimer::tick(uint32_t nowMs, ITransmitter& tx) {
  switch (state_) {
    case State::NeedRequest:
      if (sendRequest(tx)) {
        state_ = State::Waiting;
        stateMs_ = nowMs;
      }
      break;
    case State::Waiting:
      if (nowMs - stateMs_ >= kClaimWaitMs && sendClaim(candidate_, tx)) enterClaimed(nowMs);
      break;
    case State::Failed:
      if (nowMs - stateMs_ >= kFailedRetryMs) begin(rangeHigh_, nowMs);
      break;
    case State::Idle:
    case State::Claimed:
      break;
  }
}

void AddressClaimer::onAddressClaim(uint8_t sa, uint64_t theirName, uint32_t nowMs, ITransmitter& tx) {
  if (sa != candidate_) return;
  switch (state_) {
    case State::NeedRequest:
    case State::Waiting:
      if (theirName < name_) {
        advance(nowMs, tx);
      } else if (sendClaim(candidate_, tx)) {
        enterClaimed(nowMs);
      }
      break;
    case State::Claimed:
      if (theirName < name_) {
        advance(nowMs, tx);
      } else if (nowMs - lastReclaimMs_ >= kClaimWaitMs && sendClaim(candidate_, tx)) {
        // Rate-limited so an equal-NAME or non-yielding peer cannot cause a claim storm.
        lastReclaimMs_ = nowMs;
      }
      break;
    case State::Idle:
    case State::Failed:
      break;
  }
}

void AddressClaimer::onRequestForClaim(uint8_t dest, ITransmitter& tx) {
  if (state_ == State::Claimed && (dest == ADDR_GLOBAL || dest == candidate_)) {
    sendClaim(candidate_, tx);
  } else if (state_ == State::Failed && dest == ADDR_GLOBAL) {
    sendClaim(ADDR_NULL, tx);
  }
}

}  // namespace rvc
