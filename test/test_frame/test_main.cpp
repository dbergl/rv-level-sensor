#include <unity.h>

#include "dgn.h"
#include "frame.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

void test_make_id_global_dgn() {
  TEST_ASSERT_EQUAL_HEX32(0x0DFFEBB0, makeId(3, dgn::LEVELING_SENSOR_STATUS, 0xB0));
}

void test_make_id_destination_specific() {
  TEST_ASSERT_EQUAL_HEX32(0x18EAB080, makeId(6, dgn::REQUEST | 0xB0, 0x80));
}

void test_make_id_masks_priority_and_dgn() {
  TEST_ASSERT_EQUAL_HEX32(0x1DFFEBB0, makeId(0xF, 0xFFFEB, 0xB0));
}

void test_parse_id() {
  Header h = parseId(0x19FECA42);
  TEST_ASSERT_EQUAL_UINT8(6, h.priority);
  TEST_ASSERT_EQUAL_HEX32(0x1FECA, h.dgn);
  TEST_ASSERT_EQUAL_HEX8(0x42, h.sa);
}

void test_destination_helpers() {
  TEST_ASSERT_TRUE(isDestinationSpecific(0x0EAB0));
  TEST_ASSERT_FALSE(isDestinationSpecific(0x1FDE8));
  TEST_ASSERT_FALSE(isDestinationSpecific(0x0FEEB));
  TEST_ASSERT_EQUAL_HEX32(0x0EA00, baseDgn(0x0EAB0));
  TEST_ASSERT_EQUAL_HEX32(0x1FDE8, baseDgn(0x1FDE8));
  TEST_ASSERT_EQUAL_HEX8(0xB0, destinationOf(0x0EAB0));
  TEST_ASSERT_EQUAL_HEX8(0xFF, destinationOf(0x1FFEB));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_make_id_global_dgn);
  RUN_TEST(test_make_id_destination_specific);
  RUN_TEST(test_make_id_masks_priority_and_dgn);
  RUN_TEST(test_parse_id);
  RUN_TEST(test_destination_helpers);
  return UNITY_END();
}
