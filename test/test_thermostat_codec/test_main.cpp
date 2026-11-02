#include <unity.h>

#include "dgn.h"
#include "thermostat_codec.h"

using namespace rvc;

void setUp() {}
void tearDown() {}

void test_dgn_constants() {
  TEST_ASSERT_EQUAL_HEX32(0x1FFE2, dgn::THERMOSTAT_STATUS_1);
  TEST_ASSERT_EQUAL_HEX32(0x1FEF9, dgn::THERMOSTAT_COMMAND_1);
  TEST_ASSERT_EQUAL_HEX32(0x1FEFA, dgn::THERMOSTAT_STATUS_2);
  TEST_ASSERT_EQUAL_HEX32(0x1FEF8, dgn::THERMOSTAT_COMMAND_2);
  TEST_ASSERT_EQUAL_HEX32(0x1FF9C, dgn::THERMOSTAT_AMBIENT_STATUS);
  TEST_ASSERT_EQUAL_HEX32(0x0FFA0, dgn::FIREFLY_AC_OPTIONS);
  TEST_ASSERT_EQUAL_HEX32(0x0FFA1, dgn::FIREFLY_AC_OPTIONS_STATUS);
}

void test_encode_temp_rounds_up() {
  TEST_ASSERT_EQUAL_UINT16(9501, encodeTempF(75));
  for (int f = 40; f <= 100; ++f)
    TEST_ASSERT_EQUAL_INT(f, (static_cast<int>(encodeTempF(f)) - 8736) * 9 / 160 + 32);
}

void test_decode_temp() {
  TEST_ASSERT_EQUAL_INT(75, decodeTempF(9501));
  TEST_ASSERT_EQUAL_INT(61, decodeTempF(encodeTempF(61)));
  for (int f = 40; f <= 100; ++f) TEST_ASSERT_EQUAL_INT(f, decodeTempF(encodeTempF(f)));
}

void test_pack_status1_cool_fan3_72() {
  uint8_t out[8];
  packThermoStatus1({7, 1, 1, 0x78, 72}, out);
  uint16_t t = encodeTempF(72);
  const uint8_t want[8] = {0x07, 0xD1, 0x78, 0xFF, 0xFF, static_cast<uint8_t>(t & 0xFF),
                           static_cast<uint8_t>(t >> 8), 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 8);
}

void test_pack_status1_off() {
  uint8_t out[8];
  packThermoStatus1({7, 0, 3, 0, -1}, out);
  TEST_ASSERT_EQUAL_HEX8(0xF0, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[6]);
}

void test_pack_status2() {
  uint8_t out[8];
  const struct { Preset p; uint8_t b3; } cases[] = {
      {Preset::Sleep, 0xC1}, {Preset::Eco, 0xC4}, {Preset::Turbo, 0xD0}, {Preset::None, 0xC0}};
  for (const auto& c : cases) {
    packThermoStatus2(7, true, c.p, out);
    TEST_ASSERT_EQUAL_HEX8(7, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[2]);
    TEST_ASSERT_EQUAL_HEX8(c.b3, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[7]);
  }
  packThermoStatus2(7, false, Preset::Turbo, out);
  TEST_ASSERT_EQUAL_HEX8(7, out[0]);
  for (int i = 1; i < 8; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}

void test_pack_options_status() {
  uint8_t out[8];
  packOptionsStatus(7, true, false, out);
  TEST_ASSERT_EQUAL_HEX8(7, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF1, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[2]);
  packOptionsStatus(7, true, true, out);
  TEST_ASSERT_EQUAL_HEX8(0xF5, out[1]);
}

void test_pack_ambient() {
  uint8_t out[8];
  packAmbient(7, 75, out);
  const uint8_t want[8] = {0x07, 0x1D, 0x25, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 9501 = 0x251D
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 8);
}

void test_unpack_command1() {
  uint8_t in[8] = {7, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ThermoCommand1 c = unpackThermoCommand1(in);
  TEST_ASSERT_EQUAL_UINT8(7, c.instance);
  TEST_ASSERT_EQUAL_UINT8(0, c.mode);
  in[1] = 0xF1;
  TEST_ASSERT_EQUAL_UINT8(1, unpackThermoCommand1(in).mode);
  in[1] = 0xFF;
  TEST_ASSERT_EQUAL_UINT8(0x0F, unpackThermoCommand1(in).mode);
  in[1] = 0xC1;  // fan mode auto
  in[2] = 0x78;
  c = unpackThermoCommand1(in);
  TEST_ASSERT_EQUAL_UINT8(0, c.fanBits);
  TEST_ASSERT_EQUAL_HEX8(0x78, c.fanRaw);
  in[5] = 0xFA;
  in[6] = 0xFF;
  TEST_ASSERT_EQUAL_HEX16(SETPOINT_UP, unpackThermoCommand1(in).coolRaw);
  in[5] = 0xF9;
  TEST_ASSERT_EQUAL_HEX16(SETPOINT_DOWN, unpackThermoCommand1(in).coolRaw);
}

void test_unpack_command2_codes() {
  uint8_t in[8] = {7, 0xFF, 0xD0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ThermoCommand2 c = unpackThermoCommand2(in);
  TEST_ASSERT_TRUE(c.has);
  TEST_ASSERT_EQUAL_HEX8(0x10, c.code);
  in[2] = 0xFF;
  TEST_ASSERT_FALSE(unpackThermoCommand2(in).has);
}

void test_unpack_options_command() {
  uint8_t in[8] = {7, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  OptionsCommand c = unpackOptionsCommand(in);
  TEST_ASSERT_EQUAL_UINT8(1, c.display);
  TEST_ASSERT_EQUAL_UINT8(3, c.oscillate);
  in[1] = 0xFD;
  c = unpackOptionsCommand(in);
  TEST_ASSERT_EQUAL_UINT8(1, c.oscillate);
  TEST_ASSERT_EQUAL_UINT8(3, c.display);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dgn_constants);
  RUN_TEST(test_encode_temp_rounds_up);
  RUN_TEST(test_decode_temp);
  RUN_TEST(test_pack_status1_cool_fan3_72);
  RUN_TEST(test_pack_status1_off);
  RUN_TEST(test_pack_status2);
  RUN_TEST(test_pack_options_status);
  RUN_TEST(test_pack_ambient);
  RUN_TEST(test_unpack_command1);
  RUN_TEST(test_unpack_command2_codes);
  RUN_TEST(test_unpack_options_command);
  return UNITY_END();
}
