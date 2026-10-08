#include <unity.h>

#include <initializer_list>

#include "axis_config.h"
#include "block_config.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

static BlockCmdResult run(std::initializer_list<uint8_t> bytes, const level::BlockConfig& cur = {}) {
  uint8_t d[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t n = 0;
  for (uint8_t b : bytes) d[n++] = b;
  return applyBlockConfigCommand(d, n, cur);
}

static level::BlockConfig carried(uint8_t n) {
  level::BlockConfig c;
  c.blocksCarried = n;
  return c;
}

void test_settings_carry_block_defaults() {
  Settings s;
  TEST_ASSERT_TRUE(s.blocks == level::BlockConfig{});
}

void test_without_signature_ignored() {
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Ignored,
                    run({0xAA, 0x55, 0x02, 0x14, 0x0C, 0x19, 0xFF, 0xFF}).outcome);  // BMS heater command
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Ignored, run({0x4C, 0x57, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}).outcome);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Ignored, run({0x4C, 0x56}).outcome);  // too short to hold an opcode
}

// Request §5 example: block height 1.00 in, 12 carried, tolerance 0.25 in.
void test_set_blocks() {
  BlockCmdResult r = run({0x4C, 0x56, 0x02, 0x14, 0x0C, 0x19, 0xFF, 0xFF});
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  TEST_ASSERT_EQUAL_UINT8(20, r.next.blockHeight);
  TEST_ASSERT_EQUAL_UINT8(12, r.next.blocksCarried);
  TEST_ASSERT_EQUAL_UINT8(25, r.next.toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT16(1590, r.next.wheelbaseTenthIn);
}

void test_set_geometry() {
  BlockCmdResult r = run({0x4C, 0x56, 0x01, 0x78, 0x05, 0xBC, 0x02, 0xFF});  // 140.0 in, 70.0 in
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  TEST_ASSERT_EQUAL_UINT16(1400, r.next.wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(700, r.next.trackTenthIn);
  TEST_ASSERT_EQUAL_UINT8(10, r.next.blocksCarried);
}

void test_all_ones_fields_leave_unchanged() {
  level::BlockConfig cur;
  cur.wheelbaseTenthIn = 1400;
  cur.blockHeight = 30;
  BlockCmdResult r = run({0x4C, 0x56, 0x01, 0xFF, 0xFF, 0xBC, 0x02, 0xFF}, cur);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  TEST_ASSERT_EQUAL_UINT16(1400, r.next.wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(700, r.next.trackTenthIn);
  r = run({0x4C, 0x56, 0x02, 0xFF, 0x07, 0xFF, 0xFF, 0xFF}, cur);
  TEST_ASSERT_EQUAL_UINT8(30, r.next.blockHeight);
  TEST_ASSERT_EQUAL_UINT8(7, r.next.blocksCarried);
  TEST_ASSERT_EQUAL_UINT8(level::kToleranceAuto, r.next.toleranceHundredthIn);
}

// FEh sets the tolerance back to auto (half the block height); FDh is out of range.
void test_tolerance_auto_command() {
  level::BlockConfig cur;
  cur.toleranceHundredthIn = 50;
  BlockCmdResult r = run({0x4C, 0x56, 0x02, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF}, cur);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  TEST_ASSERT_EQUAL_UINT8(level::kToleranceAuto, r.next.toleranceHundredthIn);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, run({0x4C, 0x56, 0x02, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF}, cur).outcome);
}

static level::BlockConfig heightTol(uint8_t height, uint8_t tol) {
  level::BlockConfig c;
  c.blockHeight = height;
  c.toleranceHundredthIn = tol;
  return c;
}

static level::BlockConfig applied(uint8_t op, const level::BlockConfig& cur) {
  BlockCmdResult r = run({0x4C, 0x56, op, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, cur);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  return r.next;
}

// The LEVEL SETUP page's fixed frames: block height +/-0.25 in, clamped to 0.25-12.50 in.
void test_block_height_steps() {
  TEST_ASSERT_EQUAL_UINT8(25, applied(0x06, heightTol(20, level::kToleranceAuto)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(15, applied(0x07, heightTol(20, level::kToleranceAuto)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(250, applied(0x06, heightTol(248, 50)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(250, applied(0x06, heightTol(250, 50)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(5, applied(0x07, heightTol(6, 50)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(5, applied(0x07, heightTol(5, 50)).blockHeight);
  TEST_ASSERT_EQUAL_UINT8(3, applied(0x07, heightTol(3, 50)).blockHeight);  // below 0.25 in: left alone
  // Auto tolerance stays auto, so it follows the new height.
  TEST_ASSERT_EQUAL_UINT8(level::kToleranceAuto, applied(0x06, heightTol(20, level::kToleranceAuto)).toleranceHundredthIn);
}

// Tolerance +/-0.05 in, clamped to 0-2.00 in. Stepping from auto starts at the value in force.
void test_tolerance_steps() {
  TEST_ASSERT_EQUAL_UINT8(55, applied(0x08, heightTol(20, level::kToleranceAuto)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(45, applied(0x09, heightTol(20, level::kToleranceAuto)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(80, applied(0x08, heightTol(30, level::kToleranceAuto)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(0, applied(0x09, heightTol(20, 3)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(0, applied(0x09, heightTol(20, 0)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(200, applied(0x08, heightTol(20, 198)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(200, applied(0x08, heightTol(20, 200)).toleranceHundredthIn);
  // Auto with 12.50 in blocks is 6.25 in, past the 2.00 in maximum: clamped, then stepped down.
  TEST_ASSERT_EQUAL_UINT8(195, applied(0x09, heightTol(250, level::kToleranceAuto)).toleranceHundredthIn);
}

void test_tolerance_auto_op() {
  TEST_ASSERT_EQUAL_UINT8(level::kToleranceAuto, applied(0x0A, heightTol(20, 30)).toleranceHundredthIn);
  TEST_ASSERT_EQUAL_UINT8(20, applied(0x0A, heightTol(20, 30)).blockHeight);
}

static level::BlockConfig geometry(uint16_t wb, uint16_t track) {
  level::BlockConfig c;
  c.wheelbaseTenthIn = wb;
  c.trackTenthIn = track;
  return c;
}

// Wheelbase and track +/-0.5 in, clamped to 80-300 in and 40-110 in; the other stays put.
void test_geometry_steps() {
  TEST_ASSERT_EQUAL_UINT16(1595, applied(0x0B, geometry(1590, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(1585, applied(0x0C, geometry(1590, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(690, applied(0x0D, geometry(1590, 685)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(680, applied(0x0E, geometry(1590, 685)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(685, applied(0x0B, geometry(1590, 685)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(1590, applied(0x0D, geometry(1590, 685)).wheelbaseTenthIn);
  // Off the half-inch grid: steps from the value in force.
  TEST_ASSERT_EQUAL_UINT16(1598, applied(0x0B, geometry(1593, 685)).wheelbaseTenthIn);
  // Saturation at each range end.
  TEST_ASSERT_EQUAL_UINT16(3000, applied(0x0B, geometry(2998, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(3000, applied(0x0B, geometry(3000, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(800, applied(0x0C, geometry(802, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(800, applied(0x0C, geometry(800, 685)).wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(1100, applied(0x0D, geometry(1590, 1098)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(1100, applied(0x0D, geometry(1590, 1100)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(400, applied(0x0E, geometry(1590, 403)).trackTenthIn);
  TEST_ASSERT_EQUAL_UINT16(400, applied(0x0E, geometry(1590, 400)).trackTenthIn);
}

void test_carried_plus_minus_and_saturation() {
  const std::initializer_list<uint8_t> up = {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const std::initializer_list<uint8_t> down = {0x4C, 0x56, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_UINT8(11, run(up, carried(10)).next.blocksCarried);
  TEST_ASSERT_EQUAL_UINT8(9, run(down, carried(10)).next.blocksCarried);
  BlockCmdResult top = run(up, carried(250));
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, top.outcome);
  TEST_ASSERT_EQUAL_UINT8(250, top.next.blocksCarried);
  BlockCmdResult bottom = run(down, carried(0));
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, bottom.outcome);
  TEST_ASSERT_EQUAL_UINT8(0, bottom.next.blocksCarried);
}

void test_restore_defaults() {
  level::BlockConfig cur;
  cur.wheelbaseTenthIn = 2000;
  cur.blocksCarried = 3;
  BlockCmdResult r = run({0x4C, 0x56, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, cur);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, r.outcome);
  TEST_ASSERT_TRUE(r.next == level::BlockConfig{});
}

void test_out_of_range_rejected_and_unchanged() {
  level::BlockConfig cur = carried(4);
  const uint8_t bad[][8] = {
      {0x4C, 0x56, 0x01, 0x1F, 0x03, 0xFF, 0xFF, 0xFF},  // wheelbase 79.9 in
      {0x4C, 0x56, 0x01, 0xB9, 0x0B, 0xFF, 0xFF, 0xFF},  // wheelbase 300.1 in
      {0x4C, 0x56, 0x01, 0xFF, 0xFF, 0x8F, 0x01, 0xFF},  // track 39.9 in
      {0x4C, 0x56, 0x01, 0xFF, 0xFF, 0x4D, 0x04, 0xFF},  // track 110.1 in
      {0x4C, 0x56, 0x02, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},  // block height 0
      {0x4C, 0x56, 0x02, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF},  // block height 12.55 in
      {0x4C, 0x56, 0x02, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF},  // 254 carried
      {0x4C, 0x56, 0x02, 0xFF, 0xFF, 0xC9, 0xFF, 0xFF},  // tolerance 2.01 in
  };
  for (const auto& frame : bad) {
    BlockCmdResult r = applyBlockConfigCommand(frame, 8, cur);
    TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, r.outcome);
    TEST_ASSERT_TRUE(r.next == cur);
  }
}

void test_range_limits_accepted() {
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, run({0x4C, 0x56, 0x01, 0x20, 0x03, 0x90, 0x01, 0xFF}).outcome);  // 80/40 in
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, run({0x4C, 0x56, 0x01, 0xB8, 0x0B, 0x4C, 0x04, 0xFF}).outcome);  // 300/110 in
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, run({0x4C, 0x56, 0x02, 0x01, 0x00, 0x00, 0xFF, 0xFF}).outcome);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Applied, run({0x4C, 0x56, 0x02, 0xFA, 0xFA, 0xC8, 0xFF, 0xFF}).outcome);
}

void test_unknown_opcode_and_short_frame_rejected() {
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, run({0x4C, 0x56, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}).outcome);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, run({0x4C, 0x56, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}).outcome);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, run({0x4C, 0x56, 0x03}).outcome);
  TEST_ASSERT_EQUAL(BlockCmdOutcome::Rejected, run({0x4C, 0x56, 0x02, 0x14, 0x0C, 0x19, 0xFF}).outcome);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_settings_carry_block_defaults);
  RUN_TEST(test_without_signature_ignored);
  RUN_TEST(test_set_blocks);
  RUN_TEST(test_set_geometry);
  RUN_TEST(test_all_ones_fields_leave_unchanged);
  RUN_TEST(test_tolerance_auto_command);
  RUN_TEST(test_block_height_steps);
  RUN_TEST(test_tolerance_steps);
  RUN_TEST(test_tolerance_auto_op);
  RUN_TEST(test_geometry_steps);
  RUN_TEST(test_carried_plus_minus_and_saturation);
  RUN_TEST(test_restore_defaults);
  RUN_TEST(test_out_of_range_rejected_and_unchanged);
  RUN_TEST(test_range_limits_accepted);
  RUN_TEST(test_unknown_opcode_and_short_frame_rejected);
  return UNITY_END();
}
