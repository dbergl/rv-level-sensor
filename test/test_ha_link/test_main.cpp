#include <unity.h>

#include <cstdio>
#include <cstring>

#include "ha_link.h"
#include "monocypher.h"

using namespace ac;

// From `uv run tools/ac/vectors.py`.
static const char* kHaPrivHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const char* kEspPrivHex = "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f";
static const char* kHaPubHex = "8f40c5adb68f25624ae5b214ea767a6ec94d829d3d7b5e1ad1ba6f3e2138285f";
static const char* kEspPubHex = "358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254";
static const char* kSharedHex = "9663aa1da97e848a914a436d04163dfbb89178f107f1b5b77ed3854203382854";
static const char* kPairKeyHex = "c727107d2c582dd9c86f513150a56d1043ed571501b60a279fda3df22d108c45";
static const char* kNonceHex = "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
static const char* kMsgHex = "01000000020036e461c8a09b15adb32d84cfb276225c";
static const char* kMacHex = "36e461c8a09b15adb32d84cfb276225c";

static void hexTo(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = uint8_t(v);
  }
}

static uint8_t haPriv[32], espPriv[32], haPub[32], espPub[32], shared[32], pairKey[32], nonce[16],
    vecMsg[22], vecMac[16];

void setUp() {
  hexTo(kHaPrivHex, haPriv, 32);
  hexTo(kEspPrivHex, espPriv, 32);
  hexTo(kHaPubHex, haPub, 32);
  hexTo(kEspPubHex, espPub, 32);
  hexTo(kSharedHex, shared, 32);
  hexTo(kPairKeyHex, pairKey, 32);
  hexTo(kNonceHex, nonce, 16);
  hexTo(kMsgHex, vecMsg, 22);
  hexTo(kMacHex, vecMac, 16);
}
void tearDown() {}

// counter || body || mac under pairKey and nonce.
static size_t buildMsg(const uint8_t key[32], const uint8_t n16[16], uint32_t counter, const uint8_t* body,
                       size_t n, uint8_t* out) {
  for (int i = 0; i < 4; ++i) out[i] = uint8_t(counter >> (8 * i));
  memcpy(out + 4, body, n);
  commandMac(key, n16, counter, body, n, out + 4 + n);
  return 4 + n + 16;
}

void test_pair_key_matches_vector() {
  // ESP side: own private key, HA's public.
  uint8_t gotPub[32], gotKey[32];
  TEST_ASSERT_TRUE(pairEsp(espPriv, haPub, gotPub, gotKey));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(espPub, gotPub, 32);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pairKey, gotKey, 32);

  // HA side: own private key, ESP's public.
  uint8_t pub[32], sh[32], key[32];
  crypto_x25519_public_key(pub, haPriv);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(haPub, pub, 32);
  crypto_x25519(sh, haPriv, espPub);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(shared, sh, 32);
  derivePairKey(sh, haPub, espPub, key);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pairKey, key, 32);
}

void test_pair_rejects_low_order_peer_key() {
  uint8_t zero[32] = {}, gotPub[32] = {}, gotKey[32] = {};
  TEST_ASSERT_FALSE(pairEsp(espPriv, zero, gotPub, gotKey));
  uint8_t one[32] = {1};  // also low order: shared secret is all zero
  TEST_ASSERT_FALSE(pairEsp(espPriv, one, gotPub, gotKey));
  uint8_t none[32] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(none, gotKey, 32);
}

void test_command_mac_matches_vector() {
  const uint8_t body[] = {0x02, 0x00};
  uint8_t mac[16];
  commandMac(pairKey, nonce, 1, body, 2, mac);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(vecMac, mac, 16);

  uint8_t msg[22];
  TEST_ASSERT_EQUAL(22, buildMsg(pairKey, nonce, 1, body, 2, msg));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(vecMsg, msg, 22);
}

void test_session_accepts_increasing_counters() {
  Session s;
  s.begin(nonce);
  const uint8_t* body = nullptr;
  size_t len = 0;
  TEST_ASSERT_TRUE(s.accept(pairKey, vecMsg, sizeof vecMsg, body, len));
  TEST_ASSERT_EQUAL(2, len);
  TEST_ASSERT_EQUAL_PTR(vecMsg + 4, body);
  TEST_ASSERT_EQUAL_HEX8(0x02, body[0]);

  const uint8_t b2[] = {0x03, 72};
  uint8_t msg[22];
  buildMsg(pairKey, nonce, 2, b2, 2, msg);
  TEST_ASSERT_TRUE(s.accept(pairKey, msg, sizeof msg, body, len));
  TEST_ASSERT_EQUAL_HEX8(72, body[1]);

  // Gaps are fine; only order matters.
  buildMsg(pairKey, nonce, 10, b2, 2, msg);
  TEST_ASSERT_TRUE(s.accept(pairKey, msg, sizeof msg, body, len));
  TEST_ASSERT_EQUAL_UINT32(10, s.lastCounter());
}

void test_session_rejects_replay_and_bad_mac() {
  const uint8_t* body = nullptr;
  size_t len = 0;
  const uint8_t b[] = {0x00, 1};
  uint8_t msg[22];

  Session fresh;  // before begin(): nothing
  TEST_ASSERT_FALSE(fresh.accept(pairKey, vecMsg, sizeof vecMsg, body, len));

  Session s;
  s.begin(nonce);
  // Counter 0 with a valid MAC.
  buildMsg(pairKey, nonce, 0, b, 2, msg);
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, sizeof msg, body, len));

  // Same counter twice.
  TEST_ASSERT_TRUE(s.accept(pairKey, vecMsg, sizeof vecMsg, body, len));
  TEST_ASSERT_FALSE(s.accept(pairKey, vecMsg, sizeof vecMsg, body, len));

  // Lower counter after a higher one.
  buildMsg(pairKey, nonce, 5, b, 2, msg);
  TEST_ASSERT_TRUE(s.accept(pairKey, msg, sizeof msg, body, len));
  buildMsg(pairKey, nonce, 4, b, 2, msg);
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, sizeof msg, body, len));

  // One flipped MAC bit, and one flipped body bit; the rejected counter isn't consumed.
  buildMsg(pairKey, nonce, 6, b, 2, msg);
  msg[sizeof msg - 1] ^= 0x01;
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, sizeof msg, body, len));
  msg[sizeof msg - 1] ^= 0x01;
  msg[5] ^= 0x80;
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, sizeof msg, body, len));
  msg[5] ^= 0x80;
  TEST_ASSERT_TRUE(s.accept(pairKey, msg, sizeof msg, body, len));

  // Wrong key.
  uint8_t other[32];
  memcpy(other, pairKey, 32);
  other[0] ^= 1;
  buildMsg(other, nonce, 7, b, 2, msg);
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, sizeof msg, body, len));

  // Too short to hold counter and MAC.
  TEST_ASSERT_FALSE(s.accept(pairKey, msg, 19, body, len));

  // A message accepted in session A fails in session B with a new nonce, even with counter reset.
  uint8_t nonceB[16];
  memcpy(nonceB, nonce, 16);
  nonceB[15] ^= 0xFF;
  s.begin(nonceB);
  TEST_ASSERT_FALSE(s.accept(pairKey, vecMsg, sizeof vecMsg, body, len));
  buildMsg(pairKey, nonceB, 1, b, 2, msg);
  TEST_ASSERT_TRUE(s.accept(pairKey, msg, sizeof msg, body, len));
}

void test_pack_status_layout() {
  cm::UnitStatus st = {};
  st.power = true;
  st.display = false;
  st.light = true;
  st.oscillating = false;
  st.celsius = true;
  st.fan = 3;
  st.mode = cm::Mode::Turbo;
  st.setpointF = 72;
  st.airTempF = 80;
  st.batteryV = 13;
  st.lvpDeciV = 0x0177;
  for (size_t i = 0; i < cm::kFrameLen; ++i) st.raw[i] = uint8_t(0xA0 + i);

  uint8_t out[kStatusLen];
  memset(out, 0xEE, sizeof out);
  packStatus(st, true, out);
  TEST_ASSERT_EQUAL_HEX8(1, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01 | 0x02 | 0x08 | 0x20, out[1]);
  TEST_ASSERT_EQUAL_HEX8(uint8_t(cm::Mode::Turbo), out[2]);
  TEST_ASSERT_EQUAL_HEX8(3, out[3]);
  TEST_ASSERT_EQUAL_HEX8(72, out[4]);
  TEST_ASSERT_EQUAL_HEX8(80, out[5]);
  TEST_ASSERT_EQUAL_HEX8(13, out[6]);
  TEST_ASSERT_EQUAL_HEX8(0x77, out[7]);
  TEST_ASSERT_EQUAL_HEX8(0x01, out[8]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(st.raw, out + 9, cm::kFrameLen);
  const uint8_t zeros[4] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, out + 30, 4);

  st = {};
  st.display = true;
  st.oscillating = true;
  packStatus(st, false, out);
  TEST_ASSERT_EQUAL_HEX8(0x04 | 0x10, out[1]);
}

void test_unpack_command_rejects_unknown_op_and_short_body() {
  Intent in{};
  const uint8_t sp[] = {3, 72};
  TEST_ASSERT_TRUE(unpackCommand(sp, 2, in));
  TEST_ASSERT_EQUAL(uint8_t(Op::Setpoint), uint8_t(in.op));
  TEST_ASSERT_EQUAL_INT8(72, in.value);

  const uint8_t step[] = {4, 0xFF};
  TEST_ASSERT_TRUE(unpackCommand(step, 2, in));
  TEST_ASSERT_EQUAL(uint8_t(Op::StepSetpoint), uint8_t(in.op));
  TEST_ASSERT_EQUAL_INT8(-1, in.value);

  const uint8_t clear[] = {9, 0};
  TEST_ASSERT_TRUE(unpackCommand(clear, 2, in));
  TEST_ASSERT_EQUAL(uint8_t(Op::ClearPreset), uint8_t(in.op));

  const uint8_t bad[] = {10, 0};
  TEST_ASSERT_FALSE(unpackCommand(bad, 2, in));
  const uint8_t bad2[] = {0xFF, 0};
  TEST_ASSERT_FALSE(unpackCommand(bad2, 2, in));
  TEST_ASSERT_FALSE(unpackCommand(sp, 1, in));
  TEST_ASSERT_FALSE(unpackCommand(sp, 0, in));
  const uint8_t longer[] = {3, 72, 0};
  TEST_ASSERT_FALSE(unpackCommand(longer, 3, in));
}

void test_pack_result() {
  uint8_t out[kResultLen];
  packResult(0x01020304, Result::NotConfirmed, out);
  const uint8_t want[] = {0x04, 0x03, 0x02, 0x01, 3};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, kResultLen);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pair_key_matches_vector);
  RUN_TEST(test_pair_rejects_low_order_peer_key);
  RUN_TEST(test_command_mac_matches_vector);
  RUN_TEST(test_session_accepts_increasing_counters);
  RUN_TEST(test_session_rejects_replay_and_bad_mac);
  RUN_TEST(test_pack_status_layout);
  RUN_TEST(test_unpack_command_rejects_unknown_op_and_short_body);
  RUN_TEST(test_pack_result);
  return UNITY_END();
}
