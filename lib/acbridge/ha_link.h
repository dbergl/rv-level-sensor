#pragma once
#include <cstddef>
#include <cstdint>

#include "bridge.h"
#include "unit_protocol.h"

// HA <-> ESP32 link: pairing key derivation, per-command MAC with a counter bound to a
// per-connection nonce, and the status/command/result wire format. Pure logic, no heap.
namespace ac {

constexpr size_t kKeyLen = 32;
constexpr size_t kNonceLen = 16;
constexpr size_t kMacLen = 16;
constexpr size_t kCounterLen = 4;
constexpr size_t kStatusLen = 34;
constexpr size_t kResultLen = 5;
constexpr uint8_t kStatusVersion = 1;

// BLAKE2b-256 keyed with shared over "RVL-AC-PAIR-v1" || haPub || espPub.
void derivePairKey(const uint8_t shared[32], const uint8_t haPub[32], const uint8_t espPub[32],
                   uint8_t key[32]);

// ESP side of pairing: espPub from espPriv, shared secret with haPub, then the pair key. False
// (outputs untouched) on an all-zero shared secret, i.e. a low-order peer key. The shared secret
// is wiped; the caller owns espPriv and wipes it.
bool pairEsp(const uint8_t espPriv[32], const uint8_t haPub[32], uint8_t espPub[32], uint8_t key[32]);

// BLAKE2b-128 keyed with key over "RVL-AC-CMD-v1" || nonce || counter (u32 LE) || body.
void commandMac(const uint8_t key[32], const uint8_t nonce[16], uint32_t counter, const uint8_t* body,
                size_t n, uint8_t mac[16]);

// BLAKE2b-128 keyed with key over "RVL-AC-SESSION-v1" || nonce: proves the key for one link's
// session challenge.
void sessionProof(const uint8_t key[32], const uint8_t nonce[16], uint8_t out[16]);

// One connection's replay window. msg = counter (u32 LE) || body || mac16.
class Session {
 public:
  // New nonce, fresh counter: anything accepted under an earlier nonce now fails its MAC.
  void begin(const uint8_t nonce[16]);
  // True only for a valid MAC and a counter above the last accepted (so >= 1). body points into
  // msg. Nothing is accepted before begin().
  bool accept(const uint8_t key[32], const uint8_t* msg, size_t n, const uint8_t*& body, size_t& bodyLen);
  uint32_t lastCounter() const { return last_; }

 private:
  uint8_t nonce_[kNonceLen] = {};
  uint32_t last_ = 0;
  bool begun_ = false;
};

// [0] version, [1] flags (bit0 responding, bit1 power, bit2 display, bit3 light, bit4
// oscillating, bit5 celsius), [2] mode, [3] fan, [4] setpointF, [5] airTempF, [6] batteryV,
// [7..8] lvpDeciV LE, [9..29] raw frame, [30..33] reserved 0.
void packStatus(const cm::UnitStatus& s, bool responding, uint8_t out[kStatusLen]);

// body = op u8 || value i8. False for an unknown op or a body that isn't 2 bytes. tag is 0.
bool unpackCommand(const uint8_t* body, size_t n, Intent& out);

// counter (u32 LE) || result u8.
void packResult(uint32_t counter, Result r, uint8_t out[kResultLen]);

}  // namespace ac
