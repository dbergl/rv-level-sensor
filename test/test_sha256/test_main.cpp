#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "ota_sha256.h"

void setUp() {}
void tearDown() {}

static std::string hex(const uint8_t* d) {
  char s[65];
  for (int i = 0; i < 32; ++i) std::snprintf(s + 2 * i, 3, "%02x", d[i]);
  return s;
}

static std::string sha(const std::string& msg) {
  ota::Sha256 h;
  h.update(reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
  uint8_t out[32];
  h.final(out);
  return hex(out);
}

void test_empty() {
  TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", sha("").c_str());
}

void test_abc() {
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha("abc").c_str());
}

void test_two_blocks() {  // 448-bit message: padding spills into a second block
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                           sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

void test_million_a_in_odd_pieces() {
  ota::Sha256 h;
  uint8_t a[997];
  std::memset(a, 'a', sizeof a);
  size_t left = 1000000;
  size_t piece = 1;
  while (left) {
    size_t n = piece < left ? piece : left;
    h.update(a, n);
    left -= n;
    piece = piece % 900 + 37;  // 1, 38, 75, ... odd sizes, never past the 997-byte buffer
  }
  uint8_t out[32];
  h.final(out);
  TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", hex(out).c_str());
}

void test_reset_reuses() {
  ota::Sha256 h;
  uint8_t out[32];
  h.update(reinterpret_cast<const uint8_t*>("xyz"), 3);
  h.final(out);
  h.reset();
  h.update(reinterpret_cast<const uint8_t*>("abc"), 3);
  h.final(out);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex(out).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty);
  RUN_TEST(test_abc);
  RUN_TEST(test_two_blocks);
  RUN_TEST(test_million_a_in_odd_pieces);
  RUN_TEST(test_reset_reuses);
  return UNITY_END();
}
