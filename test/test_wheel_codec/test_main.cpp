#include <unity.h>

#include <cmath>

#include "dgn.h"
#include "wheel_codec.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

static WheelStatusMsg still(float pitch, float roll, const level::BlockConfig& c = {}) {
  return WheelStatusMsg{true, false, pitch, roll, level::planBlocks(pitch, roll, c)};
}

static void assertRecords(const WheelStatusMsg& m, const uint8_t r1[8], const uint8_t r2[8], const uint8_t r3[8]) {
  uint8_t out[8];
  packWheelAttitude(m, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r1, out, 8);
  packWheelLayers(m, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r2, out, 8);
  packWheelLift(m, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r3, out, 8);
}

void test_dgn_constants() {
  TEST_ASSERT_EQUAL_HEX32(0x1EFBF, dgn::LEVEL_WHEEL_STATUS);  // 1EF00h to the fixed BFh
  TEST_ASSERT_EQUAL_HEX32(0x1EF00, dgn::PROPRIETARY_A2);
}

// Request §6 test 1.
void test_check_case_records() {
  const uint8_t r1[8] = {0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06};
  const uint8_t r2[8] = {0x02, 0x03, 0x03, 0x00, 0x00, 0xFC, 0xFF, 0xFF};  // byte 5: fixed tolerance
  const uint8_t r3[8] = {0x03, 0x1C, 0x1C, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
  assertRecords(still(-1.0f, 0.0f), r1, r2, r3);
}

// Request §6 test 2.
void test_level_records() {
  const uint8_t r1[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  const uint8_t r2[8] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xFC, 0xFF, 0xFF};
  const uint8_t r3[8] = {0x03, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
  assertRecords(still(0.0f, 0.0f), r1, r2, r3);
}

// Request §3.5 / §8 item 3.
void test_fault_records() {
  WheelStatusMsg m{false, true, 3.0f, 3.0f, level::BlockPlan{}};
  const uint8_t r1[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF};
  const uint8_t r2[8] = {0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t r3[8] = {0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  assertRecords(m, r1, r2, r3);
}

// Request §6 test 3.
void test_short_on_blocks_flag() {
  level::BlockConfig c;
  c.blocksCarried = 5;
  uint8_t out[8];
  packWheelAttitude(still(-1.0f, 0.0f, c), out);
  TEST_ASSERT_EQUAL_HEX8(0x04, out[5]);  // bits 2-3 = 01b
}

void test_settling_flag() {
  WheelStatusMsg m = still(0.0f, 0.0f);
  m.moving = true;
  uint8_t out[8];
  packWheelAttitude(m, out);
  TEST_ASSERT_EQUAL_HEX8(0x41, out[5]);  // level 01b + settling 01b in bits 6-7
}

void test_roll_magnitude_and_side() {
  uint8_t out[8];
  packWheelAttitude(still(0.0f, 2.34f), out);
  TEST_ASSERT_EQUAL_UINT16(23, out[3] | out[4] << 8);
  TEST_ASSERT_EQUAL_HEX8(0x10, out[6]);  // driver side high
  packWheelAttitude(still(0.0f, -2.34f), out);
  TEST_ASSERT_EQUAL_HEX8(0x40, out[6]);  // passenger side high
}

// §8 item 1: a direction flag is set only when its magnitude field is at least 1.
void test_direction_flags_follow_rounded_magnitude() {
  uint8_t out[8];
  packWheelAttitude(still(0.04f, -0.04f), out);
  TEST_ASSERT_EQUAL_UINT16(0, out[1] | out[2] << 8);
  TEST_ASSERT_EQUAL_UINT16(0, out[3] | out[4] << 8);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);
  packWheelAttitude(still(0.05f, -0.05f), out);
  TEST_ASSERT_EQUAL_UINT16(1, out[1] | out[2] << 8);
  TEST_ASSERT_EQUAL_UINT16(1, out[3] | out[4] << 8);
  TEST_ASSERT_EQUAL_HEX8(0x41, out[6]);  // nose up + passenger side high
  packWheelAttitude(still(-0.2f, 0.2f), out);
  TEST_ASSERT_EQUAL_HEX8(0x14, out[6]);  // nose down + driver side high
}

void test_angle_tenths() {
  TEST_ASSERT_EQUAL_UINT16(12, angleTenths(1.24f));
  TEST_ASSERT_EQUAL_UINT16(13, angleTenths(-1.25f));
  TEST_ASSERT_EQUAL_UINT16(900, angleTenths(-90.0f));
  TEST_ASSERT_EQUAL_UINT16(0xFAFF, angleTenths(7000.0f));
  TEST_ASSERT_EQUAL_UINT16(0xFAFF, angleTenths(NAN));
}

void test_saturated_values_encode_fe() {
  level::BlockPlan p;
  p.liftHundredths[0] = 2504;  // 25.0 in: the largest lift that fits
  p.liftHundredths[1] = 2505;  // rounds to 25.1 in
  p.liftHundredths[2] = 0;
  p.liftHundredths[3] = level::kMaxLiftHundredths;
  p.layers[0] = 250;
  p.layers[1] = 251;
  p.layers[2] = 0;
  p.layers[3] = 10000;
  p.totalLayers = 10501;
  p.level = false;
  WheelStatusMsg m{true, false, 0.0f, 0.0f, p};
  const uint8_t r1[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE};
  const uint8_t r2[8] = {0x02, 0xFA, 0xFE, 0x00, 0xFE, 0xFC, 0xFF, 0xFF};
  const uint8_t r3[8] = {0x03, 0xFA, 0xFE, 0x00, 0xFE, 0xFF, 0xFF, 0xFF};
  assertRecords(m, r1, r2, r3);
}

void test_config_record_defaults() {
  uint8_t out[8];
  packWheelConfig(level::BlockConfig{}, out);
  const uint8_t expected[8] = {0x04, 0x36, 0x06, 0xAD, 0x02, 0x14, 0x0A, 0x32};  // auto: 0.50 in
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

// Byte 7 is the tolerance in effect: auto reports half the block height, capped at FAh.
void test_config_record_reports_effective_tolerance() {
  uint8_t out[8];
  level::BlockConfig c;
  c.blockHeight = 30;  // 1.50 in, auto -> 0.75 in
  packWheelConfig(c, out);
  TEST_ASSERT_EQUAL_HEX8(75, out[7]);
  c.toleranceHundredthIn = 25;
  packWheelConfig(c, out);
  TEST_ASSERT_EQUAL_HEX8(25, out[7]);
  c.toleranceHundredthIn = level::kToleranceAuto;
  c.blockHeight = 250;  // 12.50 in -> 6.25 in, over a byte
  packWheelConfig(c, out);
  TEST_ASSERT_EQUAL_HEX8(0xFA, out[7]);
}

// Record 02h byte 5 bits 0-1: tolerance auto 01b, fixed 00b, n/a 11b; the other bits stay 1.
void test_layers_record_auto_flag() {
  uint8_t out[8];
  WheelStatusMsg m = still(0.0f, 0.0f);
  m.toleranceAuto = true;
  packWheelLayers(m, out);
  TEST_ASSERT_EQUAL_HEX8(0xFD, out[5]);
  m.toleranceAuto = false;
  packWheelLayers(m, out);
  TEST_ASSERT_EQUAL_HEX8(0xFC, out[5]);
  m.valid = false;
  packWheelLayers(m, out);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);
  packWheelLift(still(0.0f, 0.0f), out);  // record 03h byte 5 stays reserved
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dgn_constants);
  RUN_TEST(test_layers_record_auto_flag);
  RUN_TEST(test_config_record_reports_effective_tolerance);
  RUN_TEST(test_check_case_records);
  RUN_TEST(test_level_records);
  RUN_TEST(test_fault_records);
  RUN_TEST(test_short_on_blocks_flag);
  RUN_TEST(test_settling_flag);
  RUN_TEST(test_roll_magnitude_and_side);
  RUN_TEST(test_direction_flags_follow_rounded_magnitude);
  RUN_TEST(test_angle_tenths);
  RUN_TEST(test_saturated_values_encode_fe);
  RUN_TEST(test_config_record_defaults);
  return UNITY_END();
}
