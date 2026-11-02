#include "ha_control.h"

#include <cstring>

#include "monocypher.h"

namespace ac {

namespace {

uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void badAuth(uint16_t h, const uint8_t* d, size_t n, HaControl::Out& out) {
  out.h = h;
  out.n = kResultLen;
  packResult(n >= kCounterLen ? getU32(d) : 0, Result::BadAuth, out.d);
}

}  // namespace

void HaControl::setKey(const uint8_t key[kKeyLen]) {
  memcpy(key_, key, kKeyLen);
  paired_ = true;
}

void HaControl::forget() {
  crypto_wipe(key_, sizeof key_);
  paired_ = false;
  challengeCount_ = 0;
  if (link_ != kNoLink) drop_ = link_;
  endSession();
}

void HaControl::endSession() {
  link_ = kNoLink;
  session_ = Session();
  ++sessionId_;  // in-flight results of the old session now go nowhere
}

HaControl::Auth HaControl::onAuth(uint16_t h, const uint8_t* d, size_t n, bool pairAllowed, Out& reply) {
  reply.n = 0;
  if (n == 0) return Auth::None;
  reply.h = h;
  if (d[0] == kPair) {
    reply.d[0] = kPair;
    reply.n = 1;
    if (n != 1 + kKeyLen || !pairAllowed) return Auth::Refused;
    uint8_t priv[32], pub[32], key[kKeyLen];
    fill_(priv, sizeof priv);
    bool ok = pairEsp(priv, d + 1, pub, key);
    crypto_wipe(priv, sizeof priv);
    if (!ok) return Auth::Refused;  // low-order peer key
    setKey(key);
    crypto_wipe(key, sizeof key);
    // Re-pairing (a deliberate act at the board) replaces the paired HA; its session ends.
    if (link_ != kNoLink && link_ != h) drop_ = link_;
    endSession();
    memcpy(reply.d + 1, pub, sizeof pub);
    reply.n = 1 + sizeof pub;
    return Auth::Paired;
  }
  if (d[0] == kSession) {
    reply.d[0] = kSession;
    reply.n = 1;
    if (n != 1 || !paired_) return Auth::Refused;
    int i = findChallenge(h);
    if (i < 0) {
      // Full only if links outnumber kMaxLinks: reuse the oldest slot.
      if (challengeCount_ == kMaxLinks) clearChallenge(challenges_[0].h);
      i = int(challengeCount_++);
      challenges_[i].h = h;
    }
    fill_(challenges_[i].nonce, kNonceLen);
    memcpy(reply.d + 1, challenges_[i].nonce, kNonceLen);
    reply.n = 1 + kNonceLen;
    return Auth::Challenge;
  }
  if (d[0] == kProve) {
    reply.d[0] = kProve;
    reply.d[1] = 0;
    reply.n = 2;
    int i = findChallenge(h);
    if (i < 0) return Auth::Refused;
    uint8_t nonce[kNonceLen];
    memcpy(nonce, challenges_[i].nonce, kNonceLen);
    clearChallenge(h);  // one try per challenge
    if (n != 1 + kMacLen || !paired_) return Auth::Refused;
    uint8_t want[kMacLen];
    sessionProof(key_, nonce, want);
    bool ok = crypto_verify16(want, d + 1) == 0;
    crypto_wipe(want, sizeof want);
    if (!ok) return Auth::Refused;
    if (link_ != kNoLink && link_ != h) drop_ = link_;
    endSession();
    session_.begin(nonce);
    link_ = h;
    statusSent_ = false;
    reply.d[1] = 1;
    return Auth::Session;
  }
  reply.n = 0;
  return Auth::None;
}

int HaControl::findChallenge(uint16_t h) const {
  for (size_t i = 0; i < challengeCount_; ++i)
    if (challenges_[i].h == h) return int(i);
  return -1;
}

void HaControl::clearChallenge(uint16_t h) {
  int i = findChallenge(h);
  if (i < 0) return;
  for (size_t k = size_t(i); k + 1 < challengeCount_; ++k) challenges_[k] = challenges_[k + 1];
  --challengeCount_;
}

void HaControl::onCommand(uint16_t h, const uint8_t* d, size_t n, uint32_t nowMs, Out& reply) {
  reply.n = 0;
  const uint8_t* body;
  size_t bodyLen;
  Intent in;
  if (h != link_ || !paired_ || !session_.accept(key_, d, n, body, bodyLen) ||
      !unpackCommand(body, bodyLen, in)) {
    return badAuth(h, d, n, reply);
  }
  // Full only if the bridge lost results (it drops the oldest when nobody drains): forget the
  // oldest rather than wedge.
  if (inFlightCount_ == kMaxInFlight) removeInFlight(0);
  in.tag = nextTag_++;
  if (nextTag_ == 0) nextTag_ = 1;
  inFlight_[inFlightCount_++] = {in.tag, session_.lastCounter(), sessionId_};
  bridge_.submit(in, nowMs);
}

void HaControl::onDisconnect(uint16_t h) {
  clearChallenge(h);
  if (h == link_) endSession();
  if (h == drop_) drop_ = kNoLink;
}

bool HaControl::nextResult(Out& out) {
  out.n = 0;
  uint32_t tag;
  Result r;
  while (bridge_.takeResult(tag, r)) {
    if (tag == 0) continue;  // the panel's
    size_t i = 0;
    while (i < inFlightCount_ && inFlight_[i].tag != tag) ++i;
    if (i == inFlightCount_) continue;
    InFlight f = inFlight_[i];
    removeInFlight(i);
    if (f.session != sessionId_ || link_ == kNoLink) continue;
    out.h = link_;
    out.n = kResultLen;
    packResult(f.counter, r, out.d);
    return true;
  }
  return false;
}

bool HaControl::statusDue(uint32_t nowMs, Out& out) {
  out.n = 0;
  if (link_ == kNoLink) return false;
  bool responding = bridge_.responding(nowMs);
  uint32_t version = bridge_.statusVersion();
  if (statusSent_ && version == lastVersion_ && responding == lastResponding_ &&
      nowMs - lastStatusMs_ < kStatusEveryMs) {
    return false;
  }
  statusSent_ = true;
  lastStatusMs_ = nowMs;
  lastVersion_ = version;
  lastResponding_ = responding;
  out.h = link_;
  out.n = kStatusLen;
  packStatus(bridge_.status(), responding, out.d);
  return true;
}

void HaControl::removeInFlight(size_t i) {
  for (; i + 1 < inFlightCount_; ++i) inFlight_[i] = inFlight_[i + 1];
  --inFlightCount_;
}

uint16_t HaControl::takeDrop() {
  uint16_t h = drop_;
  drop_ = kNoLink;
  return h;
}

}  // namespace ac
