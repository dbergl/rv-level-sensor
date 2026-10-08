#include <unity.h>

#include <initializer_list>

#include "axis_config.h"
#include "general_reset.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

static ResetDecision run(std::initializer_list<uint8_t> bytes) {
  uint8_t d[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t n = 0;
  for (uint8_t b : bytes) d[n++] = b;
  return decideGeneralReset(d, n);
}

static void assertDecision(const ResetDecision& r, uint8_t ack, bool reboot, bool defaults, bool window = false) {
  TEST_ASSERT_EQUAL_UINT8(ack, r.ackCode);
  TEST_ASSERT_EQUAL(reboot, r.reboot);
  TEST_ASSERT_EQUAL(defaults, r.restoreDefaults);
  TEST_ASSERT_EQUAL(window, r.openUpdateWindow);
}

void test_reboot() { assertDecision(run({0xFD, 0xFF}), ack::OK, true, false); }

// Reboot/Enter Bootloader: the "bootloader" is the Bluetooth update window; no reboot.
void test_enter_bootloader_opens_update_window() {
  assertDecision(run({0xFF, 0xDF}), ack::OK, false, false, true);
}

// Reboot and Reboot/Enter Bootloader together: the reboot wins.
void test_reboot_wins_over_enter_bootloader() {
  assertDecision(run({0xFD, 0xDF}), ack::OK, true, false, false);
}

void test_restore_defaults() { assertDecision(run({0xDF, 0xFF}), ack::OK, false, true); }

void test_defaults_and_reboot_together() { assertDecision(run({0xDD, 0xFF}), ack::OK, true, true); }

// Nothing latches, so clearing faults succeeds without doing anything.
void test_clear_faults_acks_without_action() { assertDecision(run({0xF7, 0xFF}), ack::OK, false, false); }

void test_zero_fields_are_no_action() { assertDecision(run({0x00, 0x00}), ack::OK, false, false); }

void test_unsupported_flags_nak_and_do_nothing() {
  assertDecision(run({0x7D, 0xFF}), ack::NAK, false, false);  // reset statistics + reboot
  assertDecision(run({0xFD, 0xFD}), ack::NAK, false, false);  // test mode + reboot
  assertDecision(run({0xDF, 0xF7}), ack::NAK, false, false);  // OEM reset + defaults
  assertDecision(run({0xFF, 0x7F}), ack::NAK, false, false);  // save OEM settings
}

void test_reserved_field_value_is_bad_format() {
  assertDecision(run({0xFE, 0xFF}), ack::BAD_FORMAT, false, false);  // reboot = 10b
  assertDecision(run({0xFD, 0xEF}), ack::BAD_FORMAT, false, false);  // bootloader = 10b
}

void test_short_frame_is_bad_format() { assertDecision(run({0xFD}), ack::BAD_FORMAT, false, false); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reboot);
  RUN_TEST(test_enter_bootloader_opens_update_window);
  RUN_TEST(test_reboot_wins_over_enter_bootloader);
  RUN_TEST(test_restore_defaults);
  RUN_TEST(test_defaults_and_reboot_together);
  RUN_TEST(test_clear_faults_acks_without_action);
  RUN_TEST(test_zero_fields_are_no_action);
  RUN_TEST(test_unsupported_flags_nak_and_do_nothing);
  RUN_TEST(test_reserved_field_value_is_bad_format);
  RUN_TEST(test_short_frame_is_bad_format);
  return UNITY_END();
}
