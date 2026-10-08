#include <unity.h>

#include "codec.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

void test_encode_angle() {
  TEST_ASSERT_EQUAL_UINT16(25600, encodeAngle(0.0f));
  TEST_ASSERT_EQUAL_UINT16(25728, encodeAngle(1.0f));
  TEST_ASSERT_EQUAL_UINT16(25536, encodeAngle(-0.5f));
  TEST_ASSERT_EQUAL_UINT16(0, encodeAngle(-200.0f));
  TEST_ASSERT_EQUAL_UINT16(64000, encodeAngle(300.0f));
  TEST_ASSERT_EQUAL_UINT16(25601, encodeAngle(0.004f));  // rounds to nearest 1/128
}

void test_encode_angle_clamps() {
  TEST_ASSERT_EQUAL_UINT16(0, encodeAngle(-250.0f));
  TEST_ASSERT_EQUAL_UINT16(64000, encodeAngle(400.0f));
}

void test_decode_angle() {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, decodeAngle(25728));
  TEST_ASSERT_EQUAL_FLOAT(-200.0f, decodeAngle(0));
  TEST_ASSERT_EQUAL_FLOAT(300.0f, decodeAngle(64000));
}

void test_pack_sensor_status() {
  SensorStatusMsg m{};
  m.valid = true;
  m.bits.frontLow = true;
  m.bits.passengerLow = true;
  m.pitchDeg = -1.5f;
  m.rollDeg = 2.25f;
  m.instance = 1;
  uint8_t out[8];
  packSensorStatus(m, out);
  const uint8_t expected[8] = {0x14, 0x40, 0x63, 0x20, 0x65, 0xFF, 0xFF, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

void test_pack_sensor_status_all_bits() {
  SensorStatusMsg m{};
  m.valid = true;
  m.bits.driverLow = m.bits.frontLow = m.bits.passengerLow = m.bits.rearLow = true;
  uint8_t out[8];
  packSensorStatus(m, out);
  TEST_ASSERT_EQUAL_HEX8(0x55, out[0]);
}

void test_pack_sensor_status_invalid() {
  SensorStatusMsg m{};
  m.valid = false;
  m.instance = 1;
  uint8_t out[8];
  packSensorStatus(m, out);
  const uint8_t expected[8] = {0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

void test_axis_config_round_trip() {
  AxisConfigMsg m{1, encodeAngle(1.0f), encodeAngle(0.5f), 0};
  uint8_t out[8];
  packAxisConfig(m, out);
  const uint8_t expected[8] = {0x01, 0x80, 0x64, 0x40, 0x64, 0xFC, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
  AxisConfigMsg back = unpackAxisConfig(out);
  TEST_ASSERT_EQUAL_UINT8(1, back.instance);
  TEST_ASSERT_EQUAL_UINT16(m.outRaw, back.outRaw);
  TEST_ASSERT_EQUAL_UINT16(m.withinRaw, back.withinRaw);
  TEST_ASSERT_EQUAL_UINT8(0, back.setZero);
}

void test_unpack_axis_config_set_zero() {
  const uint8_t in[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF};
  AxisConfigMsg m = unpackAxisConfig(in);
  TEST_ASSERT_EQUAL_UINT8(0, m.instance);
  TEST_ASSERT_EQUAL_UINT16(U16_NA, m.outRaw);
  TEST_ASSERT_EQUAL_UINT8(1, m.setZero);
}

void test_pack_dm_rv_no_fault() {
  uint8_t out[8];
  packDmRv({81, false, 0, 0}, out);
  const uint8_t expected[8] = {0x05, 0x51, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

void test_pack_dm_rv_fault() {
  uint8_t out[8];
  packDmRv({81, true, 2, 12}, out);
  const uint8_t expected[8] = {0x15, 0x51, 0x00, 0x00, 0x4C, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

void test_pack_dm_rv_spn_example_from_spec() {
  // Spec 3.2.5.5 example: SPN 2078 -> MSB 01h, ISB 03h, LSB bits 110b.
  uint8_t out[8];
  packDmRv({81, true, 2078, 0}, out);
  TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x03, out[3]);
  TEST_ASSERT_EQUAL_HEX8(0xC0, out[4]);
}

void test_pack_ack() {
  uint8_t out[8];
  packAck(0, 1, 0x80, 0x1FDE8, out);
  const uint8_t expected[8] = {0x00, 0x01, 0xFF, 0xFF, 0x80, 0xE8, 0xFD, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
}

void test_request_round_trip() {
  uint8_t out[8];
  packRequest(0x0EE00, out);
  const uint8_t expected[8] = {0x00, 0xEE, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
  const uint8_t in[8] = {0xEB, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0xFF, 0xFF};
  RequestMsg r = unpackRequest(in);
  TEST_ASSERT_EQUAL_HEX32(0x1FFEB, r.dgn);
  TEST_ASSERT_EQUAL_UINT8(2, r.instance);
}

void test_name() {
  uint64_t n = makeName(0x12345, 0x7AB);
  uint8_t out[8];
  packName(n, out);
  const uint8_t expected[8] = {0x45, 0x23, 0x61, 0xF5, 0x00, 0x00, 0x00, 0x80};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 8);
  TEST_ASSERT_TRUE(unpackName(out) == n);
}

void test_name_masks_fields() {
  uint64_t n = makeName(0xFFFFFFFF, 0xFFFF);
  TEST_ASSERT_TRUE(n == 0x80000000FFFFFFFFULL);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_encode_angle);
  RUN_TEST(test_encode_angle_clamps);
  RUN_TEST(test_decode_angle);
  RUN_TEST(test_pack_sensor_status);
  RUN_TEST(test_pack_sensor_status_all_bits);
  RUN_TEST(test_pack_sensor_status_invalid);
  RUN_TEST(test_axis_config_round_trip);
  RUN_TEST(test_unpack_axis_config_set_zero);
  RUN_TEST(test_pack_dm_rv_no_fault);
  RUN_TEST(test_pack_dm_rv_fault);
  RUN_TEST(test_pack_dm_rv_spn_example_from_spec);
  RUN_TEST(test_pack_ack);
  RUN_TEST(test_request_round_trip);
  RUN_TEST(test_name);
  RUN_TEST(test_name_masks_fields);
  return UNITY_END();
}
