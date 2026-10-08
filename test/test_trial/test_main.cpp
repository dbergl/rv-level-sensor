#include <unity.h>

#include "ota_trial.h"

using ota::TrialAction;
using ota::decideTrial;

void setUp() {}
void tearDown() {}

void test_not_on_trial_does_nothing() {
  TEST_ASSERT_EQUAL(TrialAction::None, decideTrial(false, 0, false, false));
  TEST_ASSERT_EQUAL(TrialAction::None, decideTrial(false, 100000, false, false));
}

void test_waits_for_30_s_even_when_healthy() {
  TEST_ASSERT_EQUAL(TrialAction::Wait, decideTrial(true, 29999, true, true));
}

void test_confirms_at_30_s_when_healthy() {
  TEST_ASSERT_EQUAL(TrialAction::Confirm, decideTrial(true, 30000, true, true));
}

void test_waits_while_not_claimed_or_no_reading() {
  TEST_ASSERT_EQUAL(TrialAction::Wait, decideTrial(true, 45000, false, true));
  TEST_ASSERT_EQUAL(TrialAction::Wait, decideTrial(true, 45000, true, false));
}

void test_rolls_back_at_60_s_when_not_healthy() {
  TEST_ASSERT_EQUAL(TrialAction::Wait, decideTrial(true, 59999, false, true));
  TEST_ASSERT_EQUAL(TrialAction::RollBack, decideTrial(true, 60000, false, true));
  TEST_ASSERT_EQUAL(TrialAction::RollBack, decideTrial(true, 60000, true, false));
}

void test_healthy_at_60_s_confirms_rather_than_rolls_back() {
  TEST_ASSERT_EQUAL(TrialAction::Confirm, decideTrial(true, 60000, true, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_not_on_trial_does_nothing);
  RUN_TEST(test_waits_for_30_s_even_when_healthy);
  RUN_TEST(test_confirms_at_30_s_when_healthy);
  RUN_TEST(test_waits_while_not_claimed_or_no_reading);
  RUN_TEST(test_rolls_back_at_60_s_when_not_healthy);
  RUN_TEST(test_healthy_at_60_s_confirms_rather_than_rolls_back);
  return UNITY_END();
}
