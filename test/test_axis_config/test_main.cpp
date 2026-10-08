#include <unity.h>

#include <cmath>

#include "axis_config.h"

using namespace rvc;

static const AxisReading kStill{true, false, 2.5f};

void setUp() {}
void tearDown() {}

static AxisConfigMsg cmd(uint8_t instance, uint16_t out, uint16_t within, uint8_t setZero) {
  return AxisConfigMsg{instance, out, within, setZero};
}

void test_validation() {
  TEST_ASSERT_TRUE(isValidAxisSettings({1.0f, 0.5f, 0.0f}));
  TEST_ASSERT_TRUE(isValidAxisSettings({0.0f, 0.0f, -90.0f}));
  TEST_ASSERT_TRUE(isValidAxisSettings({45.0f, 45.0f, 0.0f}));
  TEST_ASSERT_FALSE(isValidAxisSettings({1.0f, 1.5f, 0.0f}));   // within > out
  TEST_ASSERT_FALSE(isValidAxisSettings({46.0f, 0.5f, 0.0f}));  // out > 45
  TEST_ASSERT_FALSE(isValidAxisSettings({1.0f, -0.1f, 0.0f}));  // negative
  TEST_ASSERT_FALSE(isValidAxisSettings({1.0f, 0.5f, 91.0f}));  // zero too large
  TEST_ASSERT_FALSE(isValidAxisSettings({NAN, 0.5f, 0.0f}));
}

void test_wrong_instance_ignored() {
  ConfigResult r = applyAxisConfigCommand(cmd(2, encodeAngle(2.0f), U16_NA, 0), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Ignored, r.outcome);
  TEST_ASSERT_FALSE(r.sendAck);
}

void test_instance_zero_and_ours_accepted() {
  TEST_ASSERT_EQUAL(ConfigOutcome::Applied,
                    applyAxisConfigCommand(cmd(0, encodeAngle(2.0f), U16_NA, 0), {}, 1, kStill).outcome);
  TEST_ASSERT_EQUAL(ConfigOutcome::Applied,
                    applyAxisConfigCommand(cmd(1, encodeAngle(2.0f), U16_NA, 0), {}, 1, kStill).outcome);
}

void test_threshold_change_without_ack() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, encodeAngle(2.0f), encodeAngle(1.0f), 0), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Applied, r.outcome);
  TEST_ASSERT_FALSE(r.sendAck);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, r.next.outDeg);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, r.next.withinDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.next.zeroDeg);
}

void test_na_thresholds_keep_current() {
  AxisSettings cur{3.0f, 2.0f, 0.25f};
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, U16_NA, 3), cur, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Applied, r.outcome);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, r.next.outDeg);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, r.next.withinDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, r.next.zeroDeg);
  TEST_ASSERT_FALSE(r.sendAck);
}

void test_within_greater_than_out_rejected() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, encodeAngle(1.5f), 0), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Rejected, r.outcome);
  TEST_ASSERT_TRUE(r.sendAck);
  TEST_ASSERT_EQUAL_UINT8(ack::OUT_OF_RANGE, r.ackCode);
}

void test_out_of_range_values_rejected() {
  TEST_ASSERT_EQUAL_UINT8(ack::OUT_OF_RANGE,
                          applyAxisConfigCommand(cmd(1, encodeAngle(50.0f), U16_NA, 0), {}, 1, kStill).ackCode);
  TEST_ASSERT_EQUAL_UINT8(ack::OUT_OF_RANGE,
                          applyAxisConfigCommand(cmd(1, encodeAngle(-1.0f), encodeAngle(-2.0f), 0), {}, 1, kStill).ackCode);
  TEST_ASSERT_EQUAL_UINT8(ack::OUT_OF_RANGE,
                          applyAxisConfigCommand(cmd(1, U16_ERR, U16_NA, 0), {}, 1, kStill).ackCode);
}

void test_set_zero_acks_and_captures_raw_angle() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, U16_NA, 1), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Applied, r.outcome);
  TEST_ASSERT_TRUE(r.sendAck);
  TEST_ASSERT_EQUAL_UINT8(ack::OK, r.ackCode);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, r.next.zeroDeg);
}

void test_set_zero_while_moving_rejected() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, U16_NA, 1), {}, 1, {true, true, 2.5f});
  TEST_ASSERT_EQUAL(ConfigOutcome::Rejected, r.outcome);
  TEST_ASSERT_EQUAL_UINT8(ack::CONDITIONS, r.ackCode);
}

void test_set_zero_with_invalid_sensor_rejected() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, U16_NA, 1), {}, 1, {false, false, 0.0f});
  TEST_ASSERT_EQUAL_UINT8(ack::CONDITIONS, r.ackCode);
}

void test_set_zero_value_2_is_bad_format() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, U16_NA, U16_NA, 2), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Rejected, r.outcome);
  TEST_ASSERT_EQUAL_UINT8(ack::BAD_FORMAT, r.ackCode);
}

void test_rejected_threshold_blocks_set_zero() {
  ConfigResult r = applyAxisConfigCommand(cmd(1, encodeAngle(99.0f), U16_NA, 1), {}, 1, kStill);
  TEST_ASSERT_EQUAL(ConfigOutcome::Rejected, r.outcome);
  TEST_ASSERT_EQUAL_UINT8(ack::OUT_OF_RANGE, r.ackCode);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_validation);
  RUN_TEST(test_wrong_instance_ignored);
  RUN_TEST(test_instance_zero_and_ours_accepted);
  RUN_TEST(test_threshold_change_without_ack);
  RUN_TEST(test_na_thresholds_keep_current);
  RUN_TEST(test_within_greater_than_out_rejected);
  RUN_TEST(test_out_of_range_values_rejected);
  RUN_TEST(test_set_zero_acks_and_captures_raw_angle);
  RUN_TEST(test_set_zero_while_moving_rejected);
  RUN_TEST(test_set_zero_with_invalid_sensor_rejected);
  RUN_TEST(test_set_zero_value_2_is_bad_format);
  RUN_TEST(test_rejected_threshold_blocks_set_zero);
  return UNITY_END();
}
