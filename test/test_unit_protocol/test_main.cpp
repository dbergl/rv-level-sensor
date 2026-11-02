#include <unity.h>

#include <vector>

#include "unit_protocol.h"

using namespace cm;

void setUp() {}
void tearDown() {}

namespace {

// Status frame with the given bytes; everything else zero, checksum filled in.
void makeFrame(uint8_t f[kFrameLen], uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b8, uint8_t b9,
               uint8_t b16, uint8_t b17) {
  memset(f, 0, kFrameLen);
  f[0] = f[1] = 0x5A;
  f[2] = b2; f[3] = b3; f[4] = b4; f[8] = b8; f[9] = b9; f[16] = b16; f[17] = b17;
  uint8_t s = 0;
  for (int i = 0; i < 18; i++) s = static_cast<uint8_t>(s + f[i]);
  f[18] = s; f[19] = 0x0D; f[20] = 0x0A;
}

void goodFrame(uint8_t f[kFrameLen]) { makeFrame(f, 0x83, 0x02, 75, 52, 0x2C, 1, 72); }

struct Collector {
  std::vector<std::vector<uint8_t>> frames;
  void operator()(const uint8_t* f) { frames.emplace_back(f, f + kFrameLen); }
};

UnitStatus decode(uint8_t b2, uint8_t b3, uint8_t b4 = 75, uint8_t b8 = 52, uint8_t b16 = 1,
                  uint8_t b17 = 72) {
  uint8_t f[kFrameLen];
  makeFrame(f, b2, b3, b4, b8, 0x2C, b16, b17);
  UnitStatus s;
  TEST_ASSERT_TRUE(decodeStatus(f, s));
  return s;
}

}  // namespace

void test_build_command_power_on() {
  uint8_t out[kCommandLen];
  TEST_ASSERT_TRUE(buildCommand(0x01, 0x02, out));
  const uint8_t want[] = {0x5A, 0x5A, 0x06, 0x01, 0x01, 0x02, 0xBE, 0x0D, 0x0A};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, kCommandLen);
}

void test_allow_list() {
  TEST_ASSERT_TRUE(isAllowed(0x01, 0x00));
  TEST_ASSERT_TRUE(isAllowed(0x01, 0x01));
  TEST_ASSERT_TRUE(isAllowed(0x01, 0x02));
  const uint8_t modes[] = {0x01, 0x02, 0x04, 0x05, 0x06};
  for (uint8_t m : modes) TEST_ASSERT_TRUE(isAllowed(0x02, m));
  for (int sp = 61; sp <= 86; sp++) TEST_ASSERT_TRUE(isAllowed(0x03, sp));
  for (int fan = 1; fan <= 6; fan++) TEST_ASSERT_TRUE(isAllowed(0x04, fan));
  TEST_ASSERT_TRUE(isAllowed(0x0A, 0x01));
  TEST_ASSERT_TRUE(isAllowed(0x0A, 0x02));
  TEST_ASSERT_TRUE(isAllowed(0x45, 0x00));
  TEST_ASSERT_TRUE(isAllowed(0x45, 0x01));
  TEST_ASSERT_TRUE(isAllowed(0x1C, 0x01));
  TEST_ASSERT_TRUE(isAllowed(0x1C, 0x02));
  TEST_ASSERT_TRUE(isAllowed(0x20, 0x01));
  TEST_ASSERT_TRUE(isAllowed(0x20, 0x02));

  TEST_ASSERT_FALSE(isAllowed(0x02, 0x03));
  TEST_ASSERT_FALSE(isAllowed(0x02, 0x07));
  TEST_ASSERT_FALSE(isAllowed(0x02, 0x00));
  TEST_ASSERT_FALSE(isAllowed(0x44, 0x01));
  TEST_ASSERT_FALSE(isAllowed(0x03, 60));
  TEST_ASSERT_FALSE(isAllowed(0x03, 87));
  TEST_ASSERT_FALSE(isAllowed(0x04, 0));
  TEST_ASSERT_FALSE(isAllowed(0x04, 7));
  TEST_ASSERT_FALSE(isAllowed(0x0B, 0x01));
  TEST_ASSERT_FALSE(isAllowed(0x01, 0x03));
  TEST_ASSERT_FALSE(isAllowed(0x20, 0x00));
}

void test_build_refuses_disallowed() {
  uint8_t out[kCommandLen];
  memset(out, 0xEE, sizeof out);
  TEST_ASSERT_FALSE(buildCommand(0x02, 0x03, out));
  TEST_ASSERT_FALSE(buildCommand(0x44, 0x01, out));
  for (uint8_t b : out) TEST_ASSERT_EQUAL_UINT8(0xEE, b);
}

void test_assembler_10_10_1() {
  uint8_t f[kFrameLen];
  goodFrame(f);
  FrameAssembler a;
  Collector c;
  a.feed(f, 10, c);
  a.feed(f + 10, 10, c);
  TEST_ASSERT_EQUAL(0, c.frames.size());
  a.feed(f + 20, 1, c);
  TEST_ASSERT_EQUAL(1, c.frames.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(f, c.frames[0].data(), kFrameLen);
  TEST_ASSERT_EQUAL_UINT32(0, a.discarded());
}

void test_assembler_resyncs_after_garbage_and_lost_fragment() {
  uint8_t f[kFrameLen];
  goodFrame(f);
  FrameAssembler a;
  Collector c;
  const uint8_t junk[] = {0xFF, 0x00, 0x5A};
  a.feed(junk, 3, c);
  a.feed(f, 5, c);                 // head of a frame
  a.feed(f + 15, 6, c);            // ...middle 10 bytes lost
  a.feed(f, kFrameLen, c);         // next frame arrives whole
  TEST_ASSERT_EQUAL(1, c.frames.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(f, c.frames[0].data(), kFrameLen);
  TEST_ASSERT_TRUE(a.discarded() > 0);
  // And the stream is not wedged.
  a.feed(f, kFrameLen, c);
  TEST_ASSERT_EQUAL(2, c.frames.size());
}

void test_assembler_split_header() {
  uint8_t f[kFrameLen];
  goodFrame(f);
  FrameAssembler a;
  Collector c;
  a.feed(f, 1, c);
  a.feed(f + 1, 20, c);
  TEST_ASSERT_EQUAL(1, c.frames.size());
}

void test_assembler_bad_checksum_rejected() {
  uint8_t f[kFrameLen];
  goodFrame(f);
  f[18] ^= 0x01;
  FrameAssembler a;
  Collector c;
  a.feed(f, kFrameLen, c);
  TEST_ASSERT_EQUAL(0, c.frames.size());
  TEST_ASSERT_TRUE(a.discarded() > 0);
  goodFrame(f);
  f[20] = 0x00;  // bad trailer
  a.feed(f, kFrameLen, c);
  TEST_ASSERT_EQUAL(0, c.frames.size());
}

void test_decode_fields() {
  UnitStatus s = decode(0x80 | 0x01 | (3 << 1), 0x02 | 0x08, 75, 52, 1, 72);
  TEST_ASSERT_TRUE(s.power);
  TEST_ASSERT_TRUE(s.display);
  TEST_ASSERT_EQUAL_UINT8(3, s.fan);
  TEST_ASSERT_TRUE(s.light);
  TEST_ASSERT_TRUE(s.oscillating);
  TEST_ASSERT_FALSE(s.celsius);
  TEST_ASSERT_EQUAL_UINT8(52, s.batteryV);
  TEST_ASSERT_EQUAL_UINT16(256 + 0x2C, s.lvpDeciV);
  TEST_ASSERT_EQUAL_UINT8(75, s.airTempF);
  TEST_ASSERT_EQUAL_UINT8(72, s.setpointF);
  TEST_ASSERT_EQUAL_UINT8(72, s.setpointRaw);

  s = decode(0x00, 0x00, 75, 52, 0, 18);
  TEST_ASSERT_FALSE(s.power);
  TEST_ASSERT_FALSE(s.display);
  TEST_ASSERT_FALSE(s.light);
  TEST_ASSERT_FALSE(s.oscillating);
  TEST_ASSERT_TRUE(s.celsius);
  TEST_ASSERT_EQUAL_UINT8(0, s.fan);
}

void test_decode_mode() {
  TEST_ASSERT_TRUE(Mode::Sleep == decode(0x80, 0x10).mode);
  TEST_ASSERT_TRUE(Mode::Eco == decode(0x80, 0x20).mode);
  TEST_ASSERT_TRUE(Mode::Turbo == decode(0x80, 0x80).mode);
  TEST_ASSERT_TRUE(Mode::FanOnly == decode(0x80 | 0x30, 0x00).mode);
  TEST_ASSERT_TRUE(Mode::Dehumid == decode(0x80 | 0x20, 0x00).mode);
  TEST_ASSERT_TRUE(Mode::Cool == decode(0x80, 0x00).mode);
  TEST_ASSERT_TRUE(Mode::Cool == decode(0x80, 0x40).mode);  // vendor app connected
  TEST_ASSERT_TRUE(Mode::Eco == decode(0x80, 0x60).mode);
  TEST_ASSERT_TRUE(Mode::Unknown == decode(0x80, 0x30).mode);
}

void test_decode_out_of_range_unknown() {
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 29).airTempF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 131).airTempF);
  TEST_ASSERT_EQUAL_UINT8(30, decode(0x80, 0, 30).airTempF);
  TEST_ASSERT_EQUAL_UINT8(130, decode(0x80, 0, 130).airTempF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 75, 52, 1, 49).setpointF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 75, 52, 1, 100).setpointF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 75, 19).batteryV);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 75, 71).batteryV);
  TEST_ASSERT_EQUAL_UINT8(20, decode(0x80, 0, 75, 20).batteryV);
  TEST_ASSERT_EQUAL_UINT8(70, decode(0x80, 0, 75, 70).batteryV);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80 | (6 << 1), 0).fan);
}

void test_decode_celsius_converts() {
  UnitStatus s = decode(0x80, 0, 14, 52, 0, 18);
  TEST_ASSERT_TRUE(s.celsius);
  TEST_ASSERT_EQUAL_UINT8(57, s.airTempF);
  TEST_ASSERT_EQUAL_UINT8(64, s.setpointF);
  TEST_ASSERT_EQUAL_UINT8(18, s.setpointRaw);
}

void test_decode_celsius_garbage_unknown() {
  // 150 °C is 302 °F and 160 °C is 320 °F: truncated to a byte they'd read 46 and 64.
  UnitStatus s = decode(0x80, 0, 150, 52, 0, 160);
  TEST_ASSERT_EQUAL_UINT8(0xFF, s.airTempF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, s.setpointF);
  TEST_ASSERT_EQUAL_UINT8(0xFF, decode(0x80, 0, 0xFF, 52, 0).airTempF);
}

void test_decode_rejects_bad_frame() {
  uint8_t f[kFrameLen];
  goodFrame(f);
  UnitStatus s;
  TEST_ASSERT_TRUE(decodeStatus(f, s));
  f[18] ^= 1;
  TEST_ASSERT_FALSE(decodeStatus(f, s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_build_command_power_on);
  RUN_TEST(test_allow_list);
  RUN_TEST(test_build_refuses_disallowed);
  RUN_TEST(test_assembler_10_10_1);
  RUN_TEST(test_assembler_resyncs_after_garbage_and_lost_fragment);
  RUN_TEST(test_assembler_split_header);
  RUN_TEST(test_assembler_bad_checksum_rejected);
  RUN_TEST(test_decode_fields);
  RUN_TEST(test_decode_mode);
  RUN_TEST(test_decode_out_of_range_unknown);
  RUN_TEST(test_decode_celsius_converts);
  RUN_TEST(test_decode_celsius_garbage_unknown);
  RUN_TEST(test_decode_rejects_bad_frame);
  return UNITY_END();
}
