#include <unity.h>

#include "level_logic.h"

using namespace level;

void setUp() {}
void tearDown() {}

void test_level_inside_out_threshold() {
  LevelLogic l(1.0f, 0.5f);
  LevelBits b = l.update(0.9f, -0.9f);
  TEST_ASSERT_FALSE(b.any());
}

void test_pitch_hysteresis_front() {
  LevelLogic l(1.0f, 0.5f);
  TEST_ASSERT_TRUE(l.update(-1.1f, 0).frontLow);
  TEST_ASSERT_TRUE(l.update(-0.7f, 0).frontLow);   // between within and out: stays low
  TEST_ASSERT_TRUE(l.update(-1.0f, 0).frontLow);   // exactly at out: unchanged
  TEST_ASSERT_FALSE(l.update(-0.4f, 0).frontLow);  // inside within: clears
  TEST_ASSERT_FALSE(l.update(-0.7f, 0).frontLow);  // re-entering band does not set
}

void test_pitch_rear_low_when_nose_high() {
  LevelLogic l(1.0f, 0.5f);
  LevelBits b = l.update(1.5f, 0);
  TEST_ASSERT_TRUE(b.rearLow);
  TEST_ASSERT_FALSE(b.frontLow);
}

void test_pitch_jump_across_switches_side() {
  LevelLogic l(1.0f, 0.5f);
  l.update(-1.5f, 0);
  LevelBits b = l.update(1.5f, 0);
  TEST_ASSERT_TRUE(b.rearLow);
  TEST_ASSERT_FALSE(b.frontLow);
}

void test_roll_sides() {
  LevelLogic l(1.0f, 0.5f);
  LevelBits b = l.update(0, -2.0f);
  TEST_ASSERT_TRUE(b.driverLow);
  TEST_ASSERT_FALSE(b.passengerLow);
  b = l.update(0, 0.6f);
  TEST_ASSERT_TRUE(b.driverLow);  // not yet within 0.5
  b = l.update(0, 2.0f);
  TEST_ASSERT_TRUE(b.passengerLow);
  TEST_ASSERT_FALSE(b.driverLow);
}

void test_axes_independent_thresholds() {
  LevelLogic l(1.0f, 0.5f);
  l.setPitchThresholds(3.0f, 2.0f);
  l.setRollThresholds(0.2f, 0.1f);
  LevelBits b = l.update(-2.5f, 0.3f);
  TEST_ASSERT_FALSE(b.frontLow);
  TEST_ASSERT_TRUE(b.passengerLow);
}

void test_reset_clears_state() {
  LevelLogic l(1.0f, 0.5f);
  l.update(-2.0f, -2.0f);
  l.reset();
  TEST_ASSERT_FALSE(l.update(-0.8f, -0.8f).any());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level_inside_out_threshold);
  RUN_TEST(test_pitch_hysteresis_front);
  RUN_TEST(test_pitch_rear_low_when_nose_high);
  RUN_TEST(test_pitch_jump_across_switches_side);
  RUN_TEST(test_roll_sides);
  RUN_TEST(test_axes_independent_thresholds);
  RUN_TEST(test_reset_clears_state);
  return UNITY_END();
}
