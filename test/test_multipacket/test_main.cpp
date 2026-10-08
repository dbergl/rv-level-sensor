#include <unity.h>

#include <cstring>

#include "../support/fake_bus.h"
#include "dgn.h"
#include "multipacket.h"

using namespace rvc;

static const char* kId = "DIY*LEVEL-MPU6050*A1B2C3D4E5F6*";  // 31 bytes -> 5 data packets

void setUp() {}
void tearDown() {}

static void startId(MultiPacketTx& mp) {
  TEST_ASSERT_TRUE(mp.start(dgn::PRODUCT_ID, reinterpret_cast<const uint8_t*>(kId),
                            static_cast<uint16_t>(std::strlen(kId))));
}

void test_initial_packet_sent_immediately() {
  FakeBus bus;
  MultiPacketTx mp;
  startId(mp);
  mp.poll(1000, 0xB0, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX32(0x18ECFFB0, bus.frames[0].id);
  const uint8_t expected[8] = {0x20, 31, 0x00, 5, 0xFF, 0xEB, 0xFE, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, bus.frames[0].data, 8);
}

void test_data_packets_spaced_50ms() {
  FakeBus bus;
  MultiPacketTx mp;
  startId(mp);
  mp.poll(1000, 0xB0, bus);
  mp.poll(1049, 0xB0, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  mp.poll(1050, 0xB0, bus);
  TEST_ASSERT_EQUAL(2, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX32(0x18EBFFB0, bus.frames[1].id);
  const uint8_t expected[8] = {1, 'D', 'I', 'Y', '*', 'L', 'E', 'V'};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, bus.frames[1].data, 8);
}

void test_last_packet_padded_and_done() {
  FakeBus bus;
  MultiPacketTx mp;
  startId(mp);
  for (uint32_t t = 0; t <= 250; t += 50) mp.poll(t, 0xB0, bus);
  TEST_ASSERT_EQUAL(6, bus.frames.size());
  const uint8_t expected[8] = {5, 'F', '6', '*', 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, bus.frames[5].data, 8);
  TEST_ASSERT_FALSE(mp.busy());
  mp.poll(300, 0xB0, bus);
  TEST_ASSERT_EQUAL(6, bus.frames.size());
}

void test_rejects_while_busy_and_oversize() {
  MultiPacketTx mp;
  startId(mp);
  TEST_ASSERT_FALSE(mp.start(dgn::PRODUCT_ID, reinterpret_cast<const uint8_t*>(kId), 5));
  mp.cancel();
  uint8_t big[MultiPacketTx::kMaxLen + 1] = {};
  TEST_ASSERT_FALSE(mp.start(dgn::PRODUCT_ID, big, sizeof(big)));
  TEST_ASSERT_FALSE(mp.start(dgn::PRODUCT_ID, big, 0));
}

void test_failed_send_is_retried() {
  FakeBus bus;
  MultiPacketTx mp;
  startId(mp);
  bus.fail = true;
  mp.poll(0, 0xB0, bus);
  bus.fail = false;
  mp.poll(10, 0xB0, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.frames[0].data[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_initial_packet_sent_immediately);
  RUN_TEST(test_data_packets_spaced_50ms);
  RUN_TEST(test_last_packet_padded_and_done);
  RUN_TEST(test_rejects_while_busy_and_oversize);
  RUN_TEST(test_failed_send_is_retried);
  return UNITY_END();
}
