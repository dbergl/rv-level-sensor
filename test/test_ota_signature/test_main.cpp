#include <unity.h>

#include <cstdio>
#include <cstring>

#include "monocypher-ed25519.h"
#include "ota_sha256.h"
#include "ota_signature.h"

using namespace ota;

static void hexTo(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = uint8_t(v);
  }
}

static const char* kPubHex = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
static const char* kSigHex =
    "4678e098c9d4fa63833ed10ac0b7dad982f6d1860fb70a09b32e7bff31ec5bcb"
    "bb2745c3bb29e75418cdcd32fdcefe881697112edfa0b2fa4a8d6281403b4d00";
static const char* kShaHex = "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371";

static uint8_t pub[32], sig[64], sha[32];

void setUp() {
  hexTo(kPubHex, pub, 32);
  hexTo(kSigHex, sig, 64);
  hexTo(kShaHex, sha, 32);
}
void tearDown() {}

void test_image_hash_matches_vector() {
  uint8_t img[1000];
  for (int i = 0; i < 1000; ++i) img[i] = uint8_t(i * 7 + 3);
  Sha256 h;
  h.update(img, sizeof img);
  uint8_t got[32];
  h.final(got);
  TEST_ASSERT_EQUAL_MEMORY(sha, got, 32);
}

void test_signed_message_layout() {
  uint8_t m[kSignedMessageLen];
  signedMessage(0x01020304, sha, m);
  TEST_ASSERT_EQUAL_MEMORY("RV-Level OTA v1\0", m, 16);
  const uint8_t size[4] = {0x04, 0x03, 0x02, 0x01};
  TEST_ASSERT_EQUAL_MEMORY(size, m + 16, 4);
  TEST_ASSERT_EQUAL_MEMORY(sha, m + 20, 32);
}

void test_python_vector_verifies() { TEST_ASSERT_TRUE(verifyImage(pub, 1000, sha, sig)); }

void test_monocypher_signs_the_same_as_python() {
  uint8_t seed[32], secret[64], myPub[32], mySig[64], m[kSignedMessageLen];
  for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i);
  crypto_ed25519_key_pair(secret, myPub, seed);
  TEST_ASSERT_EQUAL_MEMORY(pub, myPub, 32);
  signedMessage(1000, sha, m);
  crypto_ed25519_sign(mySig, secret, m, sizeof m);
  TEST_ASSERT_EQUAL_MEMORY(sig, mySig, 64);
}

void test_flipped_signature_bit_fails() {
  sig[10] ^= 0x01;
  TEST_ASSERT_FALSE(verifyImage(pub, 1000, sha, sig));
}

void test_size_is_signed() { TEST_ASSERT_FALSE(verifyImage(pub, 999, sha, sig)); }

void test_hash_is_signed() {
  sha[31] ^= 0x80;
  TEST_ASSERT_FALSE(verifyImage(pub, 1000, sha, sig));
}

void test_other_key_fails() {
  pub[0] ^= 0x01;
  TEST_ASSERT_FALSE(verifyImage(pub, 1000, sha, sig));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_image_hash_matches_vector);
  RUN_TEST(test_signed_message_layout);
  RUN_TEST(test_python_vector_verifies);
  RUN_TEST(test_monocypher_signs_the_same_as_python);
  RUN_TEST(test_flipped_signature_bit_fails);
  RUN_TEST(test_size_is_signed);
  RUN_TEST(test_hash_is_signed);
  RUN_TEST(test_other_key_fails);
  return UNITY_END();
}
