#include <unity.h>

#include "bthome.h"

using namespace bthome;

void setUp() {}
void tearDown() {}

static Reading validReading() {
  Reading r{};
  r.valid = true;
  r.level = true;
  r.moving = false;
  r.problem = false;
  r.pitchDeg = -1.5f;
  r.rollDeg = 2.25f;
  return r;
}

void test_valid_reading_bytes() {
  Encoder e(7);
  uint8_t out[kMaxBody];
  size_t n = e.encode(validReading(), out, sizeof out);
  const uint8_t expected[15] = {0x40, 0x00, 0x07, 0x0F, 0x01, 0x22, 0x00, 0x26,
                                0x00, 0x3F, 0xF1, 0xFF, 0x3F, 0x17, 0x00};
  TEST_ASSERT_EQUAL(15, n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 15);
}

void test_invalid_reading_omits_rotation_and_clears_level() {
  Encoder e(0);
  Reading r = validReading();
  r.valid = false;
  r.problem = true;
  r.moving = true;
  uint8_t out[kMaxBody];
  size_t n = e.encode(r, out, sizeof out);
  const uint8_t expected[9] = {0x40, 0x00, 0x00, 0x0F, 0x00, 0x22, 0x01, 0x26, 0x01};
  TEST_ASSERT_EQUAL(9, n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 9);
}

void test_rotation_rounding() {
  TEST_ASSERT_EQUAL_INT16(13, encodeRotation(1.25f));
  TEST_ASSERT_EQUAL_INT16(-13, encodeRotation(-1.25f));
  TEST_ASSERT_EQUAL_INT16(0, encodeRotation(0.04f));
  TEST_ASSERT_EQUAL_INT16(-15, encodeRotation(-1.5f));
  TEST_ASSERT_EQUAL_INT16(23, encodeRotation(2.25f));
}

void test_rotation_clamps() {
  TEST_ASSERT_EQUAL_INT16(32767, encodeRotation(5000.0f));
  TEST_ASSERT_EQUAL_INT16(-32767, encodeRotation(-5000.0f));
}

void test_packet_id_initial_value() {
  Encoder e(200);
  uint8_t out[kMaxBody];
  e.encode(validReading(), out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(200, out[2]);
}

void test_packet_id_holds_when_content_unchanged() {
  Encoder e(5);
  uint8_t out[kMaxBody];
  e.encode(validReading(), out, sizeof out);
  e.encode(validReading(), out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(5, out[2]);
}

void test_packet_id_increments_on_change_and_wraps() {
  Encoder e(255);
  uint8_t out[kMaxBody];
  Reading r = validReading();
  e.encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(255, out[2]);
  r.pitchDeg = -1.4f;
  e.encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(0, out[2]);
  r.moving = true;
  e.encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(1, out[2]);
}

void test_sub_resolution_change_keeps_packet_id() {
  Encoder e(9);
  uint8_t out[kMaxBody];
  Reading r = validReading();
  e.encode(r, out, sizeof out);
  r.pitchDeg = -1.51f;  // still encodes as -15
  e.encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(9, out[2]);
}

void test_new_packet_forces_packet_id_on_unchanged_content() {
  // The periodic re-send uses a fresh ID so HA processes it and keeps last_reported current.
  Encoder e(5);
  uint8_t out[kMaxBody];
  e.encode(validReading(), out, sizeof out);
  e.encode(validReading(), out, sizeof out, true);
  TEST_ASSERT_EQUAL_HEX8(6, out[2]);
  e.encode(validReading(), out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(6, out[2]);
}

void test_new_packet_on_first_encode_keeps_initial_id() {
  Encoder e(5);
  uint8_t out[kMaxBody];
  e.encode(validReading(), out, sizeof out, true);
  TEST_ASSERT_EQUAL_HEX8(5, out[2]);
}

void test_small_buffer_returns_zero() {
  Encoder e(0);
  uint8_t out[8];
  TEST_ASSERT_EQUAL(0, e.encode(validReading(), out, sizeof out));
}

void test_schedule_first_refresh_due_then_throttled() {
  AdvertSchedule s(500, 10000);
  TEST_ASSERT_TRUE(s.refreshDue(1000));
  TEST_ASSERT_FALSE(s.refreshDue(1499));
  TEST_ASSERT_TRUE(s.refreshDue(1500));
}

void test_schedule_first_send_always() {
  AdvertSchedule s(500, 10000);
  TEST_ASSERT_TRUE(s.shouldSend(false, 1000));
}

void test_schedule_sends_on_change() {
  AdvertSchedule s(500, 10000);
  s.shouldSend(true, 0);
  TEST_ASSERT_TRUE(s.shouldSend(true, 500));
}

void test_schedule_reapplies_unchanged_data_periodically() {
  // Re-sending restores the advert after a NimBLE host reset clears the controller's data.
  AdvertSchedule s(500, 10000);
  s.shouldSend(true, 0);
  TEST_ASSERT_FALSE(s.shouldSend(false, 9999));
  TEST_ASSERT_TRUE(s.shouldSend(false, 10000));
  TEST_ASSERT_FALSE(s.shouldSend(false, 10500));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_reading_bytes);
  RUN_TEST(test_invalid_reading_omits_rotation_and_clears_level);
  RUN_TEST(test_rotation_rounding);
  RUN_TEST(test_rotation_clamps);
  RUN_TEST(test_packet_id_initial_value);
  RUN_TEST(test_packet_id_holds_when_content_unchanged);
  RUN_TEST(test_packet_id_increments_on_change_and_wraps);
  RUN_TEST(test_sub_resolution_change_keeps_packet_id);
  RUN_TEST(test_new_packet_forces_packet_id_on_unchanged_content);
  RUN_TEST(test_new_packet_on_first_encode_keeps_initial_id);
  RUN_TEST(test_small_buffer_returns_zero);
  RUN_TEST(test_schedule_first_refresh_due_then_throttled);
  RUN_TEST(test_schedule_first_send_always);
  RUN_TEST(test_schedule_sends_on_change);
  RUN_TEST(test_schedule_reapplies_unchanged_data_periodically);
  return UNITY_END();
}
