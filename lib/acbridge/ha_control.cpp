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
  if (link_ != kNoLink) drop_ = link_;
  endSession();
}

void HaControl::endSession() {
  link_ = kNoLink;
  session_ = Session();
  ++sessionId_;  // in-flight results of the old session now go nowhere
}

HaControl::Auth HaControl::onAuth(uint16_t h, const uint8_t* d, size_t n, bool pairOpen, Out& reply) {
  reply.n = 0;
  if (n == 0) return Auth::None;
  reply.h = h;
  if (d[0] == kPair) {
    reply.d[0] = kPair;
    reply.n = 1;
    if (n != 1 + kKeyLen || !pairOpen) return Auth::Refused;
    uint8_t priv[32], pub[32], key[kKeyLen];
    fill_(priv, sizeof priv);
    bool ok = pairEsp(priv, d + 1, pub, key);
    crypto_wipe(priv, sizeof priv);
    if (!ok) return Auth::Refused;  // low-order peer key
    setKey(key);
    crypto_wipe(key, sizeof key);
    // Re-pairing replaces the paired HA; whoever held the session must start over.
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
    if (link_ != kNoLink && link_ != h) drop_ = link_;
    endSession();
    uint8_t nonce[kNonceLen];
    fill_(nonce, sizeof nonce);
    session_.begin(nonce);
    link_ = h;
    statusSent_ = false;
    memcpy(reply.d + 1, nonce, sizeof nonce);
    reply.n = 1 + sizeof nonce;
    return Auth::Session;
  }
  reply.n = 0;
  return Auth::None;
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
