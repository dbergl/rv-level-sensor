#include <unity.h>

#include "pair_window.h"

using ac::PairWindow;

void setUp() {}
void tearDown() {}

constexpr uint32_t kLen = 120000;

void test_closed_until_opened() {
  PairWindow w(kLen);
  TEST_ASSERT_FALSE(w.isOpen(0));
  TEST_ASSERT_FALSE(w.isOpen(5000));
}

void test_open_for_its_length() {
  PairWindow w(kLen);
  w.open(1000);
  TEST_ASSERT_TRUE(w.isOpen(1000));
  TEST_ASSERT_TRUE(w.isOpen(1000 + kLen - 1));
  TEST_ASSERT_FALSE(w.isOpen(1000 + kLen));
}

void test_expiry_survives_millis_wrap() {
  PairWindow w(kLen);
  w.open(1000);
  TEST_ASSERT_FALSE(w.isOpen(1000 + kLen + 50));
  // ~49.7 days later millis() comes round to just after the open again.
  TEST_ASSERT_FALSE(w.isOpen(1000 + 10));
  TEST_ASSERT_FALSE(w.isOpen(1000 + kLen - 1));
}

void test_open_across_wrap() {
  PairWindow w(kLen);
  w.open(0xFFFFFFFFu - 1000);
  TEST_ASSERT_TRUE(w.isOpen(5000));
  TEST_ASSERT_FALSE(w.isOpen(kLen));
}

void test_close_early_and_reopen() {
  PairWindow w(kLen);
  w.open(0);
  w.close();
  TEST_ASSERT_FALSE(w.isOpen(10));
  w.open(500000);
  TEST_ASSERT_TRUE(w.isOpen(500010));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_closed_until_opened);
  RUN_TEST(test_open_for_its_length);
  RUN_TEST(test_expiry_survives_millis_wrap);
  RUN_TEST(test_open_across_wrap);
  RUN_TEST(test_close_early_and_reopen);
  return UNITY_END();
}
