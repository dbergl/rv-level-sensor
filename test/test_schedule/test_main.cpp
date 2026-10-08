#include <unity.h>

#include "schedule.h"

void setUp() {}
void tearDown() {}

// msUntilDue(now, last, period): how long the main loop may block before the next periodic
// task is due. It sleeps in the CAN receive for this long, so it must never overshoot.

void test_just_ran_waits_full_period() { TEST_ASSERT_EQUAL_UINT32(10, level::msUntilDue(1000, 1000, 10)); }

void test_part_way_waits_remainder() { TEST_ASSERT_EQUAL_UINT32(3, level::msUntilDue(1007, 1000, 10)); }

void test_due_now_does_not_wait() { TEST_ASSERT_EQUAL_UINT32(0, level::msUntilDue(1010, 1000, 10)); }

void test_overdue_does_not_wait() { TEST_ASSERT_EQUAL_UINT32(0, level::msUntilDue(1500, 1000, 10)); }

void test_millis_wraparound() {
  TEST_ASSERT_EQUAL_UINT32(6, level::msUntilDue(0x00000002u, 0xFFFFFFFEu, 10));  // 4 ms elapsed
  TEST_ASSERT_EQUAL_UINT32(0, level::msUntilDue(0x00000010u, 0xFFFFFFFEu, 10));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_just_ran_waits_full_period);
  RUN_TEST(test_part_way_waits_remainder);
  RUN_TEST(test_due_now_does_not_wait);
  RUN_TEST(test_overdue_does_not_wait);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
