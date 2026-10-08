#include <unity.h>

#include <cmath>

#include "blocks.h"

using namespace level;

void setUp() {}
void tearDown() {}

static void assertPlan(const BlockPlan& p, const uint32_t lift[4], const uint32_t layers[4]) {
  TEST_ASSERT_EQUAL_UINT32_ARRAY(lift, p.liftHundredths, 4);
  TEST_ASSERT_EQUAL_UINT32_ARRAY(layers, p.layers, 4);
}

void test_defaults_are_valid() {
  BlockConfig c;
  TEST_ASSERT_EQUAL_UINT16(1590, c.wheelbaseTenthIn);
  TEST_ASSERT_EQUAL_UINT16(685, c.trackTenthIn);
  TEST_ASSERT_EQUAL_UINT8(20, c.blockHeight);
  TEST_ASSERT_EQUAL_UINT8(10, c.blocksCarried);
  TEST_ASSERT_EQUAL_UINT8(kToleranceAuto, c.toleranceHundredthIn);  // half the block height
  TEST_ASSERT_EQUAL_UINT32(50, effectiveTolerance(c));
  TEST_ASSERT_TRUE(isValidBlockConfig(c));
}

void test_validation_boundaries() {
  BlockConfig c;
  c.wheelbaseTenthIn = 800;  TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.wheelbaseTenthIn = 799;  TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c.wheelbaseTenthIn = 3000; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.wheelbaseTenthIn = 3001; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c = BlockConfig{};
  c.trackTenthIn = 400;  TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.trackTenthIn = 399;  TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c.trackTenthIn = 1100; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.trackTenthIn = 1101; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c = BlockConfig{};
  c.blockHeight = 0;   TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c.blockHeight = 1;   TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.blockHeight = 250; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.blockHeight = 251; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c = BlockConfig{};
  c.blocksCarried = 0;   TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.blocksCarried = 250; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.blocksCarried = 251; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c = BlockConfig{};
  c.toleranceHundredthIn = 0;   TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.toleranceHundredthIn = 200; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.toleranceHundredthIn = 201; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c.toleranceHundredthIn = 0xFD; TEST_ASSERT_FALSE(isValidBlockConfig(c));
  c.toleranceHundredthIn = kToleranceAuto; TEST_ASSERT_TRUE(isValidBlockConfig(c));
  c.toleranceHundredthIn = 0xFF; TEST_ASSERT_FALSE(isValidBlockConfig(c));
}

void test_equality() {
  BlockConfig a, b;
  TEST_ASSERT_TRUE(a == b);
  b.blocksCarried = 11;
  TEST_ASSERT_TRUE(a != b);
}

// Nearest whole block, exactly half rounding down: a block is added only when it brings the wheel
// closer to level. Integer arithmetic in 0.01 in.
void test_layers_for_boundaries() {
  BlockConfig c;  // 1.00 in blocks, auto tolerance (0.50 in)
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(0, c));
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(49, c));
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(50, c));   // exactly half a block
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(51, c));
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(149, c));
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(150, c));  // exactly one and a half: rounds down
  TEST_ASSERT_EQUAL_UINT32(2, layersFor(151, c));
  c.blockHeight = 1;  // 0.05 in
  c.toleranceHundredthIn = 0;
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(2, c));  // 0.4 of a block
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(3, c));
  TEST_ASSERT_EQUAL_UINT32(2, layersFor(8, c));
}

// Auto tolerance follows the block height, so different blocks need no other change.
void test_auto_tolerance_follows_block_height() {
  BlockConfig c;
  c.blockHeight = 30;  // 1.50 in
  TEST_ASSERT_EQUAL_UINT32(75, effectiveTolerance(c));
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(75, c));
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(76, c));
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(225, c));  // 1.5 blocks: rounds down
  TEST_ASSERT_EQUAL_UINT32(2, layersFor(226, c));
}

// A fixed tolerance wider than half a block holds wheels at 0 up to it; narrower has no effect,
// because rounding to the nearest block already leaves anything under half a block alone.
void test_explicit_tolerance() {
  BlockConfig c;  // 1.00 in blocks
  c.toleranceHundredthIn = 80;
  TEST_ASSERT_EQUAL_UINT32(80, effectiveTolerance(c));
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(80, c));
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(81, c));
  c.toleranceHundredthIn = 25;
  TEST_ASSERT_EQUAL_UINT32(0, layersFor(36, c));  // the old rule gave 1 block here, overshooting
  TEST_ASSERT_EQUAL_UINT32(1, layersFor(51, c));
}

void test_flat_is_level() {
  BlockPlan p = planBlocks(0.0f, 0.0f, BlockConfig{});
  const uint32_t zero[4] = {0, 0, 0, 0};
  assertPlan(p, zero, zero);
  TEST_ASSERT_EQUAL_UINT32(0, p.totalLayers);
  TEST_ASSERT_TRUE(p.level);
  TEST_ASSERT_FALSE(p.shortOnBlocks);
}

// Request §4 check case: front 1 deg low, WB 159 in -> front lift 2.78 in, 3 layers each.
void test_check_case_front_low() {
  BlockPlan p = planBlocks(-1.0f, 0.0f, BlockConfig{});
  const uint32_t lift[4] = {278, 278, 0, 0};
  const uint32_t layers[4] = {3, 3, 0, 0};
  assertPlan(p, lift, layers);
  TEST_ASSERT_EQUAL_UINT32(6, p.totalLayers);
  TEST_ASSERT_FALSE(p.level);
  TEST_ASSERT_FALSE(p.shortOnBlocks);
}

// +roll = driver side up, so the passenger wheels come up. 68.5 in track, 0.5 deg -> 0.60 in.
void test_pure_roll() {
  BlockPlan p = planBlocks(0.0f, 0.5f, BlockConfig{});
  const uint32_t lift[4] = {0, 60, 0, 60};
  const uint32_t layers[4] = {0, 1, 0, 1};
  assertPlan(p, lift, layers);
  TEST_ASSERT_FALSE(p.level);
}

// Nose up 1.5 deg, passenger side up 2 deg: FR is highest.
void test_combined_tilt() {
  BlockPlan p = planBlocks(1.5f, -2.0f, BlockConfig{});
  const uint32_t lift[4] = {239, 0, 656, 416};
  const uint32_t layers[4] = {2, 0, 7, 4};  // 2.39 -> 2, 6.56 -> 7, 4.16 -> 4
  assertPlan(p, lift, layers);
  TEST_ASSERT_EQUAL_UINT32(13, p.totalLayers);
  TEST_ASSERT_TRUE(p.shortOnBlocks);  // 13 > 10 carried
}

void test_lift_within_tolerance_is_level() {
  BlockPlan p = planBlocks(0.0f, -0.2f, BlockConfig{});  // driver wheels lift 0.24 in
  TEST_ASSERT_EQUAL_UINT32(24, p.liftHundredths[kFrontLeft]);
  TEST_ASSERT_EQUAL_UINT32(0, p.layers[kFrontLeft]);
  TEST_ASSERT_TRUE(p.level);
}

void test_short_on_blocks_boundary() {
  BlockConfig c;
  c.blocksCarried = 6;
  TEST_ASSERT_FALSE(planBlocks(-1.0f, 0.0f, c).shortOnBlocks);  // 6 layers, 6 carried
  c.blocksCarried = 5;
  TEST_ASSERT_TRUE(planBlocks(-1.0f, 0.0f, c).shortOnBlocks);
}

void test_geometry_is_used() {
  BlockConfig c;
  c.wheelbaseTenthIn = 3000;  // 300 in: front lift 300 * tan 1 deg = 5.24 in
  BlockPlan p = planBlocks(-1.0f, 0.0f, c);
  TEST_ASSERT_EQUAL_UINT32(524, p.liftHundredths[kFrontLeft]);
  TEST_ASSERT_EQUAL_UINT32(5, p.layers[kFrontLeft]);  // nearest to 5.24
}

void test_extreme_tilt_saturates() {
  BlockPlan p = planBlocks(90.0f, 0.0f, BlockConfig{});  // nose straight up: rear wheels "need" infinity
  TEST_ASSERT_EQUAL_UINT32(kMaxLiftHundredths, p.liftHundredths[kRearLeft]);
  TEST_ASSERT_EQUAL_UINT32(kMaxLiftHundredths, p.liftHundredths[kRearRight]);
  TEST_ASSERT_EQUAL_UINT32(0, p.liftHundredths[kFrontLeft]);
  TEST_ASSERT_EQUAL_UINT32(10000, p.layers[kRearLeft]);
  TEST_ASSERT_EQUAL_UINT32(20000, p.totalLayers);
  TEST_ASSERT_TRUE(p.shortOnBlocks);
}

void test_nan_angle_saturates() {
  BlockPlan p = planBlocks(NAN, 0.0f, BlockConfig{});
  for (int w = 0; w < kWheelCount; ++w) TEST_ASSERT_EQUAL_UINT32(kMaxLiftHundredths, p.liftHundredths[w]);
  TEST_ASSERT_FALSE(p.level);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_validation_boundaries);
  RUN_TEST(test_equality);
  RUN_TEST(test_layers_for_boundaries);
  RUN_TEST(test_auto_tolerance_follows_block_height);
  RUN_TEST(test_explicit_tolerance);
  RUN_TEST(test_flat_is_level);
  RUN_TEST(test_check_case_front_low);
  RUN_TEST(test_pure_roll);
  RUN_TEST(test_combined_tilt);
  RUN_TEST(test_lift_within_tolerance_is_level);
  RUN_TEST(test_short_on_blocks_boundary);
  RUN_TEST(test_geometry_is_used);
  RUN_TEST(test_extreme_tilt_saturates);
  RUN_TEST(test_nan_angle_saturates);
  return UNITY_END();
}
