#include <unity.h>

#include "../support/fake_bus.h"
#include "address_claim.h"
#include "codec.h"
#include "dgn.h"

using namespace rvc;

static const uint64_t kOurs = makeName(0x1234, 0);
static const uint64_t kBetter = kOurs - 1;  // lower value wins
static const uint64_t kWorse = kOurs + 1;

void setUp() {}
void tearDown() {}

static AddressClaimer make() { return AddressClaimer(kOurs, 191, 176); }

void test_empty_bus_claims_highest_address() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX32(0x18EABFFE, bus.frames[0].id);  // request to 191 from FEh
  const uint8_t req[8] = {0x00, 0xEE, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(req, bus.frames[0].data, 8);
  c.tick(249, bus);
  TEST_ASSERT_FALSE(c.claimed());
  c.tick(250, bus);
  TEST_ASSERT_TRUE(c.claimed());
  TEST_ASSERT_EQUAL_UINT8(191, c.address());
  TEST_ASSERT_EQUAL_HEX32(0x18EE00BF, bus.frames[1].id);
  TEST_ASSERT_TRUE(unpackName(bus.frames[1].data) == kOurs);
}

void test_preferred_address_used_when_in_range() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(180, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  TEST_ASSERT_EQUAL_UINT8(180, c.address());
}

void test_preferred_address_out_of_range_ignored() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(42, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  TEST_ASSERT_EQUAL_UINT8(191, c.address());
}

void test_no_address_before_claim() {
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  TEST_ASSERT_EQUAL_HEX8(ADDR_NULL, c.address());
}

void test_higher_priority_competitor_moves_us_down() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.onAddressClaim(191, kBetter, 10, bus);
  c.tick(10, bus);  // requests 190
  TEST_ASSERT_EQUAL_HEX32(0x18EABEFE, bus.frames.back().id);
  c.tick(260, bus);
  TEST_ASSERT_EQUAL_UINT8(190, c.address());
}

void test_lower_priority_competitor_loses() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.onAddressClaim(191, kWorse, 10, bus);
  TEST_ASSERT_TRUE(c.claimed());
  TEST_ASSERT_EQUAL_UINT8(191, c.address());
  TEST_ASSERT_EQUAL_HEX32(0x18EE00BF, bus.frames.back().id);
}

void test_claims_for_other_addresses_ignored() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.onAddressClaim(190, kBetter, 10, bus);
  c.tick(250, bus);
  TEST_ASSERT_EQUAL_UINT8(191, c.address());
}

void test_conflict_after_claim_we_win_reclaims() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  bus.clear();
  c.onAddressClaim(191, kWorse, 1000, bus);
  TEST_ASSERT_TRUE(c.claimed());
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX32(0x18EE00BF, bus.frames[0].id);
}

void test_conflict_after_claim_we_lose_moves() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  c.onAddressClaim(191, kBetter, 1000, bus);
  TEST_ASSERT_FALSE(c.claimed());
  TEST_ASSERT_EQUAL_HEX8(ADDR_NULL, c.address());
  c.tick(1000, bus);
  c.tick(1250, bus);
  TEST_ASSERT_EQUAL_UINT8(190, c.address());
}

void test_wraps_from_zero_to_253() {
  FakeBus bus;
  AddressClaimer c(kOurs, 1, 0);
  c.begin(0, 0);
  c.tick(0, bus);
  c.onAddressClaim(0, kBetter, 0, bus);
  c.tick(0, bus);
  TEST_ASSERT_EQUAL_HEX32(0x18EAFDFE, bus.frames.back().id);  // request to 253
}

void test_exhaustion_sends_cannot_claim_and_retries() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  uint8_t sa = 191;
  for (int i = 0; i < 254; ++i) {
    c.tick(0, bus);
    c.onAddressClaim(sa, kBetter, 0, bus);
    sa = sa == 0 ? 253 : sa - 1;
  }
  TEST_ASSERT_TRUE(c.failed());
  TEST_ASSERT_EQUAL_HEX32(0x18EE00FE, bus.frames.back().id);
  bus.clear();
  c.onRequestForClaim(ADDR_GLOBAL, bus);
  TEST_ASSERT_EQUAL_HEX32(0x18EE00FE, bus.frames.back().id);
  c.tick(29999, bus);
  TEST_ASSERT_TRUE(c.failed());
  c.tick(30000, bus);
  c.tick(30000, bus);
  TEST_ASSERT_EQUAL_HEX32(0x18EABFFE, bus.frames.back().id);
}

void test_request_for_claim_answered_when_claimed() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.onRequestForClaim(ADDR_GLOBAL, bus);  // still waiting: no answer
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  c.tick(250, bus);
  bus.clear();
  c.onRequestForClaim(ADDR_GLOBAL, bus);
  c.onRequestForClaim(191, bus);
  c.onRequestForClaim(100, bus);
  TEST_ASSERT_EQUAL(2, bus.frames.size());
}

void test_send_failure_retries_request() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  bus.fail = true;
  c.tick(0, bus);
  bus.fail = false;
  c.tick(10, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  c.tick(259, bus);
  TEST_ASSERT_FALSE(c.claimed());  // wait restarts from the successful request
  c.tick(260, bus);
  TEST_ASSERT_TRUE(c.claimed());
}

void test_ready_only_250ms_after_claim() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  TEST_ASSERT_TRUE(c.claimed());
  TEST_ASSERT_FALSE(c.ready(250));
  TEST_ASSERT_FALSE(c.ready(499));
  TEST_ASSERT_TRUE(c.ready(500));
}

void test_reclaim_rebroadcast_rate_limited() {
  FakeBus bus;
  AddressClaimer c = make();
  c.begin(0xFF, 0);
  c.tick(0, bus);
  c.tick(250, bus);
  bus.clear();
  for (uint32_t t = 1000; t < 1250; t += 10) c.onAddressClaim(191, kWorse, t, bus);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  c.onAddressClaim(191, kWorse, 1250, bus);
  TEST_ASSERT_EQUAL(2, bus.frames.size());
  TEST_ASSERT_TRUE(c.claimed());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_bus_claims_highest_address);
  RUN_TEST(test_preferred_address_used_when_in_range);
  RUN_TEST(test_preferred_address_out_of_range_ignored);
  RUN_TEST(test_no_address_before_claim);
  RUN_TEST(test_higher_priority_competitor_moves_us_down);
  RUN_TEST(test_lower_priority_competitor_loses);
  RUN_TEST(test_claims_for_other_addresses_ignored);
  RUN_TEST(test_conflict_after_claim_we_win_reclaims);
  RUN_TEST(test_conflict_after_claim_we_lose_moves);
  RUN_TEST(test_wraps_from_zero_to_253);
  RUN_TEST(test_exhaustion_sends_cannot_claim_and_retries);
  RUN_TEST(test_request_for_claim_answered_when_claimed);
  RUN_TEST(test_send_failure_retries_request);
  RUN_TEST(test_ready_only_250ms_after_claim);
  RUN_TEST(test_reclaim_rebroadcast_rate_limited);
  return UNITY_END();
}
