#include <unity.h>

#include <vector>

#include "ac_panel.h"
#include "dgn.h"
#include "../support/fake_bus.h"
#include "thermostat_codec.h"

using cm::Mode;
using namespace rvc;
using namespace rvc::dgn;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kSa = 0x99;
constexpr uint8_t kPanelSa = 0x50;

struct Unit {
  bool power = true;
  Mode mode = Mode::Cool;
  uint8_t fan = 0;
  uint8_t sp = 70;
  bool display = true;
  bool osc = false;
  uint8_t air = 75;
};

void encode(const Unit& u, uint8_t f[cm::kFrameLen]) {
  memset(f, 0, cm::kFrameLen);
  f[0] = f[1] = 0x5A;
  f[2] = static_cast<uint8_t>((u.power ? 0x80 : 0) | (u.display ? 0x01 : 0) | (u.fan << 1) |
                              (u.mode == Mode::FanOnly ? 0x30 : 0));
  f[3] = static_cast<uint8_t>((u.osc ? 0x08 : 0) | (u.mode == Mode::Sleep ? 0x10 : 0) |
                              (u.mode == Mode::Eco ? 0x20 : 0) | (u.mode == Mode::Turbo ? 0x80 : 0));
  f[4] = u.air;
  f[8] = 52;
  f[9] = 0x2C;
  f[16] = 1;
  f[17] = u.sp;
  uint8_t s = 0;
  for (int i = 0; i < 18; i++) s = static_cast<uint8_t>(s + f[i]);
  f[18] = s;
  f[19] = 0x0D;
  f[20] = 0x0A;
}

void apply(Unit& u, uint8_t func, uint8_t data) {
  switch (func) {
    case 0x01: if (data) u.power = data == 0x02; break;
    case 0x02:
      u.mode = data == 0x01 ? Mode::Cool : data == 0x02 ? Mode::FanOnly : data == 0x04 ? Mode::Eco
             : data == 0x05 ? Mode::Sleep : Mode::Turbo;
      break;
    case 0x03: u.sp = data; break;
    case 0x04: u.fan = data == 0x06 ? 0 : data; break;
    case 0x0A: u.display = data == 0x02; break;
    case 0x45: u.osc = data == 0x01; break;
  }
}

struct Write {
  uint8_t func, data;
  uint32_t at = 0;
};

struct FakeLink : ac::IUnitLink {
  explicit FakeLink(const uint32_t& clock) : clock_(clock) {}
  const uint32_t& clock_;
  bool up = true;
  std::vector<Write> writes;
  bool connected() const override { return up; }
  bool write(const uint8_t* d, size_t) override {
    writes.push_back({d[4], d[5], clock_});
    return true;
  }
};

struct Rig {
  uint32_t now = 0;
  FakeLink link{now};
  ac::Bridge br{link};
  FakeBus bus;
  AcPanel panel{br, bus};
  Unit unit;
  size_t answered = 0;
  bool silentUnit = false;

  void advance(uint32_t ms) {
    for (uint32_t end = now + ms; now < end;) {
      now += 10;
      br.tick(now);
      if (!silentUnit && answered < link.writes.size() && now - link.writes[answered].at >= 50) {
        const Write& w = link.writes[answered++];
        apply(unit, w.func, w.data);
        sendStatus();
      }
      panel.update(true, kSa, now);
    }
  }
  void sendStatus() {
    uint8_t f[cm::kFrameLen];
    encode(unit, f);
    br.onNotify(f, cm::kFrameLen, now);
  }
  void ready() {
    advance(200);
    TEST_ASSERT_TRUE(br.responding(now));
    bus.clear();
  }
  // Commands (non-poll) the unit received.
  std::vector<Write> commands() const {
    std::vector<Write> out;
    for (const Write& w : link.writes)
      if (!(w.func == 0x01 && w.data == 0x00)) out.push_back(w);
    return out;
  }
  void press(uint32_t dgn, const uint8_t d[8], uint8_t sa = kPanelSa) {
    CanFrame f;
    f.id = makeId(6, dgn, sa);
    for (int i = 0; i < 8; ++i) f.data[i] = d[i];
    panel.onFrame(f, now);
    advance(3000);
  }
  void cmd1(uint8_t inst, uint8_t mode, uint8_t fanBits, uint8_t fanRaw, uint16_t cool) {
    uint8_t d[8] = {inst, static_cast<uint8_t>(mode | (fanBits << 4) | 0xC0), fanRaw, 0xFF, 0xFF,
                    static_cast<uint8_t>(cool & 0xFF), static_cast<uint8_t>(cool >> 8), 0xFF};
    press(THERMOSTAT_COMMAND_1, d);
  }
  void cmd2(uint8_t code) {
    uint8_t d[8] = {7, 0xFF, code, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    press(THERMOSTAT_COMMAND_2, d);
  }
};

void expectWrite(const Write& w, uint8_t func, uint8_t data) {
  TEST_ASSERT_EQUAL_HEX8(func, w.func);
  TEST_ASSERT_EQUAL_HEX8(data, w.data);
}

}  // namespace

void test_silent_until_responding() {
  Rig r;
  r.panel.update(true, kSa, 0);
  TEST_ASSERT_EQUAL(0, r.bus.frames.size());
  r.advance(200);
  TEST_ASSERT_EQUAL(1, r.bus.count(THERMOSTAT_STATUS_1));
  TEST_ASSERT_EQUAL(1, r.bus.count(THERMOSTAT_AMBIENT_STATUS));
  TEST_ASSERT_EQUAL(1, r.bus.count(THERMOSTAT_STATUS_2));
  TEST_ASSERT_EQUAL(1, r.bus.count(FIREFLY_AC_OPTIONS_STATUS));
  TEST_ASSERT_EQUAL(4, r.bus.frames.size());
  TEST_ASSERT_EQUAL(0, r.bus.count(0x1FFE1));
  TEST_ASSERT_EQUAL_UINT8(6, parseId(r.bus.frames[0].id).priority);
  TEST_ASSERT_EQUAL_UINT8(kSa, parseId(r.bus.frames[0].id).sa);
}

void test_not_claimed_is_silent() {
  Rig r;
  r.advance(200);
  r.bus.clear();
  r.now += 10000;
  r.sendStatus();
  r.panel.update(false, kSa, r.now);
  TEST_ASSERT_EQUAL(0, r.bus.frames.size());
}

void test_heartbeat_5s_and_on_change() {
  Rig r;
  r.ready();
  r.advance(4000);
  TEST_ASSERT_EQUAL(0, r.bus.count(THERMOSTAT_STATUS_1));
  r.advance(2000);
  TEST_ASSERT_EQUAL(1, r.bus.count(THERMOSTAT_STATUS_1));
  r.bus.clear();
  r.unit.sp = 72;
  r.sendStatus();
  r.panel.update(true, kSa, r.now);
  TEST_ASSERT_EQUAL(1, r.bus.count(THERMOSTAT_STATUS_1));
  TEST_ASSERT_EQUAL(1, r.bus.count(FIREFLY_AC_OPTIONS_STATUS));
}

void test_goes_silent_when_unit_lost() {
  Rig r;
  r.ready();
  r.silentUnit = true;
  r.link.up = false;
  r.advance(12000);
  r.bus.clear();
  r.advance(10000);
  TEST_ASSERT_EQUAL(0, r.bus.frames.size());
}

void test_status_mapping_eco_fan3_72() {
  Rig r;
  r.unit.mode = Mode::Eco;
  r.unit.fan = 3;
  r.unit.sp = 72;
  r.advance(200);
  auto s1 = r.bus.withDgn(THERMOSTAT_STATUS_1).at(0);
  TEST_ASSERT_EQUAL_HEX8(7, s1.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0xD1, s1.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x78, s1.data[2]);
  uint16_t raw = static_cast<uint16_t>(s1.data[5] | (s1.data[6] << 8));
  TEST_ASSERT_EQUAL_UINT16(encodeTempF(72), raw);
  TEST_ASSERT_EQUAL_HEX8(0xC4, r.bus.withDgn(THERMOSTAT_STATUS_2).at(0).data[3]);
  auto amb = r.bus.withDgn(THERMOSTAT_AMBIENT_STATUS).at(0);
  TEST_ASSERT_EQUAL_UINT16(encodeTempF(75), static_cast<uint16_t>(amb.data[1] | (amb.data[2] << 8)));
}

void test_status_off() {
  Rig r;
  r.unit.power = false;
  r.advance(200);
  auto s1 = r.bus.withDgn(THERMOSTAT_STATUS_1).at(0);
  TEST_ASSERT_EQUAL_HEX8(0x30 | 0xC0, s1.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0, s1.data[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, r.bus.withDgn(THERMOSTAT_STATUS_2).at(0).data[3]);
}

void test_panel_cool_fan_setpoint() {
  Rig r;
  r.unit.mode = Mode::FanOnly;
  r.ready();
  r.cmd1(7, 1, 1, 0x78, encodeTempF(72));
  r.advance(3000);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(3, c.size());
  expectWrite(c[0], 0x02, 0x01);
  expectWrite(c[1], 0x04, 0x03);
  expectWrite(c[2], 0x03, 0x48);
}

void test_panel_mode_cool_from_fan_writes() {
  Rig r;
  r.unit.mode = Mode::FanOnly;
  r.ready();
  r.cmd1(7, 1, 3, 0xFF, 0xFFFF);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x02, 0x01);
}

void test_panel_ignores_heat_dry_and_other_instances() {
  Rig r;
  r.ready();
  r.cmd1(7, 2, 3, 0xFF, 0xFFFF);
  r.cmd1(7, 6, 3, 0xFF, 0xFFFF);
  r.cmd1(7, 3, 3, 0xFF, 0xFFFF);
  r.cmd1(6, 4, 0, 0xFF, encodeTempF(65));
  TEST_ASSERT_EQUAL(0, r.commands().size());
}

void test_panel_setpoint_up_at_86_no_write() {
  Rig r;
  r.unit.sp = 86;
  r.ready();
  r.cmd1(7, 0x0F, 3, 0xFF, SETPOINT_UP);
  TEST_ASSERT_EQUAL(0, r.commands().size());
  r.unit.sp = 61;
  r.sendStatus();
  r.cmd1(7, 0x0F, 3, 0xFF, SETPOINT_DOWN);
  TEST_ASSERT_EQUAL(0, r.commands().size());
  r.cmd1(7, 0x0F, 3, 0xFF, SETPOINT_UP);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x03, 62);
}

void test_panel_power_off() {
  Rig r;
  r.ready();
  r.cmd1(7, 0, 3, 0xFF, 0xFFFF);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x01, 0x01);
}

void test_panel_preset_toggle() {
  Rig r;
  r.unit.mode = Mode::Eco;
  r.ready();
  r.cmd2(0x04);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x02, 0x01);
}

void test_panel_preset_code0_clears() {
  Rig r;
  r.unit.mode = Mode::Eco;
  r.ready();
  r.cmd2(0x00);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x02, 0x01);
}

void test_panel_preset_select_and_unknown_code() {
  Rig r;
  r.ready();
  r.cmd2(0x10);
  r.cmd2(0x05);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x02, 0x06);
}

void test_panel_option_toggle() {
  Rig r;
  r.ready();
  uint8_t d[8] = {7, static_cast<uint8_t>(0xF0 | (1 << 2) | 3), 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  r.press(FIREFLY_AC_OPTIONS, d);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x0A, 0x01);
}

void test_own_sa_ignored() {
  Rig r;
  r.ready();
  uint8_t d[8] = {7, 0xFF, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  r.press(THERMOSTAT_COMMAND_2, d, kSa);
  TEST_ASSERT_EQUAL(0, r.commands().size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silent_until_responding);
  RUN_TEST(test_not_claimed_is_silent);
  RUN_TEST(test_heartbeat_5s_and_on_change);
  RUN_TEST(test_goes_silent_when_unit_lost);
  RUN_TEST(test_status_mapping_eco_fan3_72);
  RUN_TEST(test_status_off);
  RUN_TEST(test_panel_cool_fan_setpoint);
  RUN_TEST(test_panel_mode_cool_from_fan_writes);
  RUN_TEST(test_panel_ignores_heat_dry_and_other_instances);
  RUN_TEST(test_panel_setpoint_up_at_86_no_write);
  RUN_TEST(test_panel_power_off);
  RUN_TEST(test_panel_preset_toggle);
  RUN_TEST(test_panel_preset_code0_clears);
  RUN_TEST(test_panel_preset_select_and_unknown_code);
  RUN_TEST(test_panel_option_toggle);
  RUN_TEST(test_own_sa_ignored);
  return UNITY_END();
}
