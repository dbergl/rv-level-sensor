#include "ha_link.h"

#include <cstring>

#include "monocypher.h"

namespace ac {

namespace {

const char kPairLabel[] = "RVL-AC-PAIR-v1";
const char kCmdLabel[] = "RVL-AC-CMD-v1";

void putU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool allZero(const uint8_t* p, size_t n) {
  uint8_t acc = 0;
  for (size_t i = 0; i < n; ++i) acc |= p[i];
  return acc == 0;
}

}  // namespace

void derivePairKey(const uint8_t shared[32], const uint8_t haPub[32], const uint8_t espPub[32],
                   uint8_t key[32]) {
  crypto_blake2b_ctx ctx;
  crypto_blake2b_keyed_init(&ctx, kKeyLen, shared, 32);
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kPairLabel), sizeof kPairLabel - 1);
  crypto_blake2b_update(&ctx, haPub, 32);
  crypto_blake2b_update(&ctx, espPub, 32);
  crypto_blake2b_final(&ctx, key);  // wipes ctx
}

bool pairEsp(const uint8_t espPriv[32], const uint8_t haPub[32], uint8_t espPub[32], uint8_t key[32]) {
  uint8_t shared[32];
  crypto_x25519(shared, espPriv, haPub);
  // Low-order peer key: the "secret" is known to anyone. HA's side refuses it too.
  if (allZero(shared, sizeof shared)) {
    crypto_wipe(shared, sizeof shared);
    return false;
  }
  uint8_t pub[32];
  crypto_x25519_public_key(pub, espPriv);
  derivePairKey(shared, haPub, pub, key);
  crypto_wipe(shared, sizeof shared);
  memcpy(espPub, pub, 32);
  return true;
}

void commandMac(const uint8_t key[32], const uint8_t nonce[16], uint32_t counter, const uint8_t* body,
                size_t n, uint8_t mac[16]) {
  uint8_t ctr[kCounterLen];
  putU32(ctr, counter);
  crypto_blake2b_ctx ctx;
  crypto_blake2b_keyed_init(&ctx, kMacLen, key, kKeyLen);
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kCmdLabel), sizeof kCmdLabel - 1);
  crypto_blake2b_update(&ctx, nonce, kNonceLen);
  crypto_blake2b_update(&ctx, ctr, sizeof ctr);
  crypto_blake2b_update(&ctx, body, n);
  crypto_blake2b_final(&ctx, mac);
}

void Session::begin(const uint8_t nonce[16]) {
  memcpy(nonce_, nonce, kNonceLen);
  last_ = 0;
  begun_ = true;
}

bool Session::accept(const uint8_t key[32], const uint8_t* msg, size_t n, const uint8_t*& body,
                     size_t& bodyLen) {
  if (!begun_ || n < kCounterLen + kMacLen) return false;
  const uint32_t counter = getU32(msg);
  if (counter <= last_) return false;  // replay, or counter 0
  const size_t len = n - kCounterLen - kMacLen;
  uint8_t mac[kMacLen];
  commandMac(key, nonce_, counter, msg + kCounterLen, len, mac);
  const bool ok = crypto_verify16(mac, msg + kCounterLen + len) == 0;
  crypto_wipe(mac, sizeof mac);
  if (!ok) return false;
  last_ = counter;
  body = msg + kCounterLen;
  bodyLen = len;
  return true;
}

void packStatus(const cm::UnitStatus& s, bool responding, uint8_t out[kStatusLen]) {
  memset(out, 0, kStatusLen);
  out[0] = kStatusVersion;
  out[1] = uint8_t((responding ? 0x01 : 0) | (s.power ? 0x02 : 0) | (s.display ? 0x04 : 0) |
                   (s.light ? 0x08 : 0) | (s.oscillating ? 0x10 : 0) | (s.celsius ? 0x20 : 0));
  out[2] = uint8_t(s.mode);
  out[3] = s.fan;
  out[4] = s.setpointF;
  out[5] = s.airTempF;
  out[6] = s.batteryV;
  out[7] = uint8_t(s.lvpDeciV);
  out[8] = uint8_t(s.lvpDeciV >> 8);
  memcpy(out + 9, s.raw, cm::kFrameLen);
}

bool unpackCommand(const uint8_t* body, size_t n, Intent& out) {
  if (n != 2 || body[0] > uint8_t(Op::ClearPreset)) return false;
  out.op = Op(body[0]);
  out.value = int8_t(body[1]);
  out.tag = 0;
  return true;
}

void packResult(uint32_t counter, Result r, uint8_t out[kResultLen]) {
  putU32(out, counter);
  out[4] = uint8_t(r);
}

}  // namespace ac
