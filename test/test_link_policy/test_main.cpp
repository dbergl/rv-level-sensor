#include <unity.h>

#include <cstring>

#include "link_policy.h"

using namespace ac;

void setUp() {}
void tearDown() {}

void test_backoff_doubles_from_1s_to_30s() {
  Backoff b;
  const uint32_t want[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
  for (uint32_t w : want) TEST_ASSERT_EQUAL_UINT32(w, b.next());
}

void test_backoff_reset_starts_over() {
  Backoff b;
  b.next();
  b.next();
  b.reset();
  TEST_ASSERT_EQUAL_UINT32(1000, b.next());
}

const uint8_t kA[6] = {1, 2, 3, 4, 5, 6};
const uint8_t kB[6] = {9, 8, 7, 6, 5, 4};

void offer(BindPicker& p, const char* name, const uint8_t* addr, int rssi, uint8_t type = 0) {
  p.offer(name, std::strlen(name), addr, type, rssi);
}

void test_picker_nothing_found() {
  BindPicker p;
  BindPicker::Candidate c;
  TEST_ASSERT_FALSE(p.best(c));
}

void test_picker_ignores_names_not_starting_kt() {
  BindPicker p;
  offer(p, "RV Level", kA, -40);
  offer(p, "kT2026", kA, -40);
  offer(p, "K", kA, -40);
  p.offer(nullptr, 0, kA, 0, -40);
  BindPicker::Candidate c;
  TEST_ASSERT_FALSE(p.best(c));
}

void test_picker_takes_strongest_kt() {
  BindPicker p;
  offer(p, "KT2026010000007", kA, -80);
  offer(p, "KT2025010000001", kB, -60, 1);
  offer(p, "Other", kA, -10);
  BindPicker::Candidate c;
  TEST_ASSERT_TRUE(p.best(c));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kB, c.addr, 6);
  TEST_ASSERT_EQUAL_UINT8(1, c.type);
  TEST_ASSERT_EQUAL_INT8(-60, c.rssi);
  TEST_ASSERT_EQUAL_STRING("KT2025010000001", c.name);
}

void test_picker_weaker_reading_of_best_keeps_its_peak() {
  BindPicker p;
  offer(p, "KTA", kA, -50);
  offer(p, "KTB", kB, -60);
  offer(p, "KTA", kA, -90);
  BindPicker::Candidate c;
  TEST_ASSERT_TRUE(p.best(c));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kA, c.addr, 6);
  TEST_ASSERT_EQUAL_INT8(-50, c.rssi);
}

void test_picker_tie_keeps_first() {
  BindPicker p;
  offer(p, "KTA", kA, -60);
  offer(p, "KTB", kB, -60);
  BindPicker::Candidate c;
  TEST_ASSERT_TRUE(p.best(c));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kA, c.addr, 6);
}

void test_picker_truncates_long_name_and_resets() {
  BindPicker p;
  offer(p, "KT0123456789012345678901234567890", kA, -60);
  BindPicker::Candidate c;
  TEST_ASSERT_TRUE(p.best(c));
  TEST_ASSERT_EQUAL_size_t(BindPicker::kNameMax, std::strlen(c.name));
  p.reset();
  TEST_ASSERT_FALSE(p.best(c));
}

void test_ring_fifo_and_wrap() {
  ByteRing<4> r;
  const uint8_t a[] = {1, 2, 3};
  TEST_ASSERT_EQUAL_size_t(3, r.push(a, 3));
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(2, r.pop(out, 2));
  TEST_ASSERT_EQUAL_UINT8(1, out[0]);
  TEST_ASSERT_EQUAL_UINT8(2, out[1]);
  const uint8_t b[] = {4, 5, 6};
  TEST_ASSERT_EQUAL_size_t(3, r.push(b, 3));  // wraps
  TEST_ASSERT_EQUAL_size_t(4, r.pop(out, 4));
  const uint8_t want[] = {3, 4, 5, 6};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 4);
  TEST_ASSERT_EQUAL_size_t(0, r.size());
}

void test_ring_full_drops_newest() {
  ByteRing<4> r;
  const uint8_t a[] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_EQUAL_size_t(4, r.push(a, 6));
  TEST_ASSERT_EQUAL_UINT32(2, r.dropped());
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(4, r.pop(out, 4));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a, out, 4);
  r.push(a, 2);
  r.clear();
  TEST_ASSERT_EQUAL_size_t(0, r.pop(out, 4));
}

void test_address_shown_most_significant_first() {
  const uint8_t native[6] = {0x9A, 0x78, 0x56, 0x34, 0x12, 0x02};  // as NimBLE stores it
  char text[kAddrTextLen];
  formatAddress(native, text);
  TEST_ASSERT_EQUAL_STRING("02:12:34:56:78:9A", text);
}

void test_picker_keeps_native_byte_order() {
  // scan -> candidate -> NVS -> connect must never reorder the bytes.
  const uint8_t native[6] = {0x9A, 0x78, 0x56, 0x34, 0x12, 0x02};
  BindPicker p;
  p.offer("KT1", 3, native, 1, -50);
  BindPicker::Candidate c;
  TEST_ASSERT_TRUE(p.best(c));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(native, c.addr, 6);
  char text[kAddrTextLen];
  formatAddress(c.addr, text);
  TEST_ASSERT_EQUAL_STRING("02:12:34:56:78:9A", text);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_backoff_doubles_from_1s_to_30s);
  RUN_TEST(test_backoff_reset_starts_over);
  RUN_TEST(test_picker_nothing_found);
  RUN_TEST(test_picker_ignores_names_not_starting_kt);
  RUN_TEST(test_picker_takes_strongest_kt);
  RUN_TEST(test_picker_weaker_reading_of_best_keeps_its_peak);
  RUN_TEST(test_picker_tie_keeps_first);
  RUN_TEST(test_picker_truncates_long_name_and_resets);
  RUN_TEST(test_ring_fifo_and_wrap);
  RUN_TEST(test_ring_full_drops_newest);
  RUN_TEST(test_address_shown_most_significant_first);
  RUN_TEST(test_picker_keeps_native_byte_order);
  return UNITY_END();
}
