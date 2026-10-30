#include <unity.h>

#include "led_duty.h"

void setUp() {}
void tearDown() {}

// The board's RGB LED is common-anode (anode to 3.3 V, cathodes to the GPIOs through 470 R),
// so a color is lit while its pin is LOW: pin duty = 255 - brightness.

void test_off_holds_pin_high() {
  TEST_ASSERT_EQUAL_UINT8(255, ledPinDuty(false, 100));
  TEST_ASSERT_EQUAL_UINT8(255, ledPinDuty(false, 3));
}

void test_full_brightness_holds_pin_low() { TEST_ASSERT_EQUAL_UINT8(0, ledPinDuty(true, 100)); }

void test_dimmed_is_low_for_brightness_fraction() {
  TEST_ASSERT_EQUAL_UINT8(255 - 7, ledPinDuty(true, 3));   // 3 % of 255 = 7
  TEST_ASSERT_EQUAL_UINT8(255 - 25, ledPinDuty(true, 10));
}

void test_zero_brightness_is_off() { TEST_ASSERT_EQUAL_UINT8(255, ledPinDuty(true, 0)); }

void test_brightness_clamps_to_100() { TEST_ASSERT_EQUAL_UINT8(0, ledPinDuty(true, 200)); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_holds_pin_high);
  RUN_TEST(test_full_brightness_holds_pin_low);
  RUN_TEST(test_dimmed_is_low_for_brightness_fraction);
  RUN_TEST(test_zero_brightness_is_off);
  RUN_TEST(test_brightness_clamps_to_100);
  return UNITY_END();
}
