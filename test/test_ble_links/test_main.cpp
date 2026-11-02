#include <unity.h>

#include "ble_links.h"

using namespace blelink;

void setUp() {}
void tearDown() {}

void test_connectable_only_when_wanted_and_two_slots_free() {
  Links l(4, 30000);
  TEST_ASSERT_FALSE(l.connectable(false));
  TEST_ASSERT_TRUE(l.connectable(true));
  TEST_ASSERT_TRUE(l.onConnect(1, 0));
  TEST_ASSERT_TRUE(l.onConnect(2, 0));
  TEST_ASSERT_TRUE(l.connectable(true));
  TEST_ASSERT_TRUE(l.onConnect(3, 0));
  TEST_ASSERT_FALSE(l.connectable(true));  // 3 of 4: the last slot stays free
}

void test_client_links_count() {
  Links l(4, 30000);
  l.setClientLinks(1);
  TEST_ASSERT_TRUE(l.onConnect(1, 0));
  TEST_ASSERT_TRUE(l.connectable(true));
  TEST_ASSERT_TRUE(l.onConnect(2, 0));
  TEST_ASSERT_FALSE(l.connectable(true));
}

void test_link_past_the_cap_is_dropped_on_connect() {
  Links l(3, 30000);  // leveling-only build: max 3
  TEST_ASSERT_TRUE(l.onConnect(1, 0));
  TEST_ASSERT_TRUE(l.onConnect(2, 0));
  TEST_ASSERT_FALSE(l.onConnect(3, 0));
  TEST_ASSERT_EQUAL(3, l.serverLinks());  // counted until its disconnect arrives
  TEST_ASSERT_FALSE(l.connectable(true));
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(0));  // already being dropped
  l.onDisconnect(3);
  TEST_ASSERT_EQUAL(2, l.serverLinks());
}

void test_unclaimed_link_dropped_after_timeout() {
  Links l(4, 30000);
  l.onConnect(5, 1000);
  l.onConnect(6, 2000);
  l.claim(6);
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(30999));
  TEST_ASSERT_EQUAL_UINT16(5, l.toDrop(31000));
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(31000));  // once only
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(100000));  // 6 is claimed
}

void test_claimed_before_timeout_is_kept() {
  Links l(4, 30000);
  l.onConnect(5, 0);
  l.claim(5);
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(60000));
}

void test_timeout_survives_millis_wrap() {
  Links l(4, 30000);
  l.onConnect(5, 0xFFFFF000u);
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(0x1000));
  TEST_ASSERT_EQUAL_UINT16(5, l.toDrop(0xFFFFF000u + 30000));
}

void test_client_link_over_cap_drops_newest_unclaimed_server_link() {
  Links l(4, 30000);
  l.onConnect(1, 0);
  l.onConnect(2, 0);
  l.onConnect(3, 0);
  l.claim(1);
  l.claim(3);
  l.setClientLinks(1);  // total 4 of 4
  TEST_ASSERT_EQUAL_UINT16(2, l.toDrop(0));
  TEST_ASSERT_EQUAL_UINT16(kNoLink, l.toDrop(0));  // back at the cap once 2 leaves
}

void test_client_link_over_cap_drops_newest_when_all_claimed() {
  Links l(4, 30000);
  l.onConnect(1, 0);
  l.onConnect(2, 0);
  l.onConnect(3, 0);
  l.claim(1);
  l.claim(2);
  l.claim(3);
  l.setClientLinks(1);
  TEST_ASSERT_EQUAL_UINT16(3, l.toDrop(0));
}

void test_disconnect_frees_slot_and_unknown_handles_ignored() {
  Links l(3, 30000);
  l.onConnect(1, 0);
  l.onConnect(2, 0);
  TEST_ASSERT_FALSE(l.connectable(true));
  l.onDisconnect(9);
  TEST_ASSERT_EQUAL(2, l.serverLinks());
  l.claim(9);
  l.onDisconnect(1);
  TEST_ASSERT_EQUAL(1, l.serverLinks());
  TEST_ASSERT_TRUE(l.connectable(true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_connectable_only_when_wanted_and_two_slots_free);
  RUN_TEST(test_client_links_count);
  RUN_TEST(test_link_past_the_cap_is_dropped_on_connect);
  RUN_TEST(test_unclaimed_link_dropped_after_timeout);
  RUN_TEST(test_claimed_before_timeout_is_kept);
  RUN_TEST(test_timeout_survives_millis_wrap);
  RUN_TEST(test_client_link_over_cap_drops_newest_unclaimed_server_link);
  RUN_TEST(test_client_link_over_cap_drops_newest_when_all_claimed);
  RUN_TEST(test_disconnect_frees_slot_and_unknown_handles_ignored);
  return UNITY_END();
}
