#include <unity.h>

#include <vector>

#include "bridge.h"

using ac::Bridge;
using ac::Intent;
using ac::Op;
using ac::Result;
using cm::Mode;

void setUp() {}
void tearDown() {}

namespace {

// Simulated unit state, encoded into a status frame per Signal sheet A.
struct Unit {
  bool power = false;
  Mode mode = Mode::Cool;
  uint8_t fan = 0;
  uint8_t sp = 70;  // in the unit's own scale
  bool celsius = false;
  bool display = true;
  bool light = false;
  bool osc = false;
  bool appBit = false;  // 0x40 in byte 3: not a decoded field
  uint8_t air = 75;
};

void encode(const Unit& u, uint8_t f[cm::kFrameLen]) {
  memset(f, 0, cm::kFrameLen);
  f[0] = f[1] = 0x5A;
  f[2] = static_cast<uint8_t>((u.power ? 0x80 : 0) | (u.display ? 0x01 : 0) | (u.fan << 1) |
                              (u.mode == Mode::FanOnly ? 0x30 : 0));
  f[3] = static_cast<uint8_t>((u.light ? 0x02 : 0) | (u.osc ? 0x08 : 0) | (u.appBit ? 0x40 : 0) |
                              (u.mode == Mode::Sleep ? 0x10 : 0) | (u.mode == Mode::Eco ? 0x20 : 0) |
                              (u.mode == Mode::Turbo ? 0x80 : 0));
  f[4] = u.air;
  f[8] = 52;
  f[9] = 0x2C;
  f[16] = u.celsius ? 0 : 1;
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
    case 0x1C: u.light = data == 0x01; break;
    case 0x20: u.celsius = data == 0x01; break;
  }
}

struct Write {
  uint8_t func, data;
  uint32_t at;
};

struct FakeLink : ac::IUnitLink {
  explicit FakeLink(const uint32_t& clock) : clock_(clock) {}
  bool up = true;
  std::vector<Write> writes;
  bool connected() const override { return up; }
  bool write(const uint8_t* d, size_t n) override {
    // Invariants for every test: whole command frames, allow-listed, 400 ms apart.
    TEST_ASSERT_EQUAL(cm::kCommandLen, n);
    TEST_ASSERT_TRUE(cm::isAllowed(d[4], d[5]));
    if (!writes.empty()) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(400, clock_ - writes.back().at);
    writes.push_back({d[4], d[5], clock_});
    return true;
  }
  const uint32_t& clock_;
};

bool isPoll(const Write& w) { return w.func == 0x01 && w.data == 0x00; }

struct Rig {
  uint32_t now = 0;
  FakeLink link{now};
  Bridge br{link};
  Unit unit;
  bool autoReply = true;    // the unit answers each write with a status frame
  bool applyWrites = true;  // the unit obeys commands
  size_t answered = 0;

  void sendStatus() {
    uint8_t f[cm::kFrameLen];
    encode(unit, f);
    // Two fragments, as BLE notifications tend to arrive.
    br.onNotify(f, 10, now);
    br.onNotify(f + 10, cm::kFrameLen - 10, now);
  }

  // Ticks every 10 ms; the unit replies 50 ms after each write.
  void advance(uint32_t ms) {
    for (uint32_t end = now + ms; now < end;) {
      now += 10;
      br.tick(now);
      while (answered < link.writes.size() && now - link.writes[answered].at >= 50) {
        const Write& w = link.writes[answered++];
        if (applyWrites) apply(unit, w.func, w.data);
        if (autoReply) sendStatus();
      }
    }
  }

  // First poll written and answered: the bridge is responding.
  void ready() {
    advance(100);
    TEST_ASSERT_TRUE(br.responding(now));
  }

  std::vector<Write> commands() const {
    std::vector<Write> out;
    for (const Write& w : link.writes)
      if (!isPoll(w)) out.push_back(w);
    return out;
  }

  void submit(Op op, int v, uint32_t tag) { br.submit(Intent{op, static_cast<int8_t>(v), tag}, now); }

  void expectResult(uint32_t tag, Result r) {
    uint32_t t;
    Result got;
    TEST_ASSERT_TRUE_MESSAGE(br.takeResult(t, got), "no result");
    TEST_ASSERT_EQUAL_UINT32(tag, t);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(r), static_cast<uint8_t>(got));
  }
  void expectNoResult() {
    uint32_t t;
    Result got;
    TEST_ASSERT_FALSE(br.takeResult(t, got));
  }
};

void expectWrite(const Write& w, uint8_t func, uint8_t data) {
  TEST_ASSERT_EQUAL_HEX8(func, w.func);
  TEST_ASSERT_EQUAL_HEX8(data, w.data);
}

}  // namespace

void test_polls_every_3s_with_400ms_gap() {
  Rig r;
  r.advance(10);
  TEST_ASSERT_EQUAL(1, r.link.writes.size());
  expectWrite(r.link.writes[0], 0x01, 0x00);
  r.advance(50);  // reply lands at 60
  r.advance(2990);  // now 3050
  TEST_ASSERT_EQUAL(1, r.link.writes.size());
  r.advance(10);  // 3060 = reply + 3000
  TEST_ASSERT_EQUAL(2, r.link.writes.size());
  expectWrite(r.link.writes[1], 0x01, 0x00);
  TEST_ASSERT_EQUAL_UINT32(3060, r.link.writes[1].at);

  // A command right after a poll still waits out the gap (FakeLink asserts it).
  r.advance(60);
  r.submit(Op::Power, 1, 1);
  r.advance(1000);
  TEST_ASSERT_EQUAL(3, r.link.writes.size());
  TEST_ASSERT_EQUAL_UINT32(3460, r.link.writes[2].at);
  r.expectResult(1, Result::Ok);
}

void test_cool_from_off_powers_on_then_mode() {
  Rig r;
  r.unit.mode = Mode::Eco;  // the mode reported while off isn't trusted
  r.ready();
  r.submit(Op::Mode, static_cast<int>(Mode::Cool), 7);
  r.advance(2000);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(2, c.size());
  expectWrite(c[0], 0x01, 0x02);
  expectWrite(c[1], 0x02, 0x01);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(400, c[1].at - c[0].at);
  r.expectResult(7, Result::Ok);
  TEST_ASSERT_TRUE(r.br.status().power);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Cool), static_cast<int>(r.br.status().mode));
}

void test_cool_while_eco_is_noop() {
  Rig r;
  r.unit.power = true;
  r.unit.mode = Mode::Eco;
  r.ready();
  r.submit(Op::Mode, static_cast<int>(Mode::Cool), 3);
  r.advance(100);
  TEST_ASSERT_EQUAL(0, r.commands().size());
  r.expectResult(3, Result::Ok);
}

void test_preset_while_off_refused() {
  Rig r;
  r.ready();
  r.submit(Op::Mode, static_cast<int>(Mode::Eco), 4);
  r.advance(100);
  TEST_ASSERT_EQUAL(0, r.commands().size());
  r.expectResult(4, Result::Refused);
}

void test_clear_preset() {
  Rig r;
  r.unit.power = true;
  r.unit.mode = Mode::Eco;
  r.ready();
  r.submit(Op::ClearPreset, 0, 1);
  r.advance(1000);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  expectWrite(c[0], 0x02, 0x01);
  r.expectResult(1, Result::Ok);

  // Plain cool: nothing to clear.
  r.submit(Op::ClearPreset, 0, 2);
  r.advance(100);
  TEST_ASSERT_EQUAL(1, r.commands().size());
  r.expectResult(2, Result::Ok);

  r.unit.power = false;
  r.sendStatus();
  r.submit(Op::ClearPreset, 0, 3);
  r.advance(100);
  TEST_ASSERT_EQUAL(1, r.commands().size());
  r.expectResult(3, Result::Refused);
}

void test_step_setpoint_counts_from_pending() {
  Rig r;
  r.unit.power = true;
  r.unit.sp = 70;
  r.ready();
  r.submit(Op::StepSetpoint, 1, 1);
  r.submit(Op::StepSetpoint, 1, 2);
  r.submit(Op::StepSetpoint, 1, 3);
  r.advance(3000);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(3, c.size());
  expectWrite(c[0], 0x03, 0x47);
  expectWrite(c[1], 0x03, 0x48);
  expectWrite(c[2], 0x03, 0x49);
  r.expectResult(1, Result::Ok);
  r.expectResult(2, Result::Ok);
  r.expectResult(3, Result::Ok);
  r.expectNoResult();

  // Nothing pending any more: counts from the reported 73.
  r.submit(Op::StepSetpoint, -1, 4);
  r.advance(1000);
  expectWrite(r.commands().back(), 0x03, 72);
  r.expectResult(4, Result::Ok);
}

void test_setpoint_refused_in_celsius_and_out_of_range() {
  Rig r;
  r.unit.power = true;
  r.unit.celsius = true;
  r.unit.sp = 22;
  r.ready();
  r.submit(Op::Setpoint, 72, 1);
  r.expectResult(1, Result::Refused);  // immediate
  r.submit(Op::StepSetpoint, 1, 2);
  r.expectResult(2, Result::Refused);

  r.unit.celsius = false;
  r.unit.sp = 86;
  r.sendStatus();
  r.submit(Op::StepSetpoint, 1, 3);
  r.expectResult(3, Result::Refused);
  r.submit(Op::Setpoint, 60, 4);
  r.expectResult(4, Result::Refused);
  r.submit(Op::Setpoint, 87, 5);
  r.expectResult(5, Result::Refused);
  r.advance(1000);
  TEST_ASSERT_EQUAL(0, r.commands().size());
  r.expectNoResult();
}

void test_reply_differs_reports_not_confirmed_without_loop() {
  Rig r;
  r.unit.power = true;
  r.unit.mode = Mode::Cool;
  r.ready();
  // Someone uses the remote: the unit ignores us and goes to fan only.
  r.applyWrites = false;
  r.unit.mode = Mode::FanOnly;
  r.submit(Op::Mode, static_cast<int>(Mode::Eco), 9);
  r.advance(1500);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(2, c.size());  // the command and one retry
  expectWrite(c[0], 0x02, 0x04);
  expectWrite(c[1], 0x02, 0x04);
  r.expectResult(9, Result::NotConfirmed);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::FanOnly), static_cast<int>(r.br.status().mode));
  r.advance(10000);
  TEST_ASSERT_EQUAL(2, r.commands().size());
  r.expectNoResult();
}

void test_timeout_retries_once_then_fails() {
  Rig r;
  r.unit.power = true;
  r.ready();
  r.autoReply = false;
  r.submit(Op::Power, 0, 5);
  r.advance(500);
  auto c = r.commands();
  TEST_ASSERT_EQUAL(1, c.size());
  const uint32_t first = c[0].at;
  r.advance(first + 2990 - r.now);
  TEST_ASSERT_EQUAL(1, r.commands().size());
  r.expectNoResult();
  r.advance(10);
  c = r.commands();
  TEST_ASSERT_EQUAL(2, c.size());
  expectWrite(c[1], 0x01, 0x01);
  TEST_ASSERT_EQUAL_UINT32(first + 3000, c[1].at);
  r.advance(2990);
  r.expectNoResult();
  r.advance(10);
  r.expectResult(5, Result::NotConfirmed);
  r.advance(5000);
  TEST_ASSERT_EQUAL(2, r.commands().size());
}

void test_unreachable_fails_fast_and_never_replays() {
  Rig r;
  r.link.up = false;
  r.advance(100);
  TEST_ASSERT_EQUAL(0, r.link.writes.size());
  r.submit(Op::Power, 1, 1);
  r.expectResult(1, Result::Unreachable);

  r.link.up = true;
  r.submit(Op::Power, 1, 2);  // connected, but no frame yet
  r.expectResult(2, Result::Unreachable);
  r.ready();
  r.autoReply = false;
  r.submit(Op::Power, 1, 3);
  r.submit(Op::Fan, 2, 4);
  r.submit(Op::Light, 1, 5);
  r.advance(500);
  TEST_ASSERT_EQUAL(1, r.commands().size());
  r.link.up = false;
  r.advance(10);
  r.expectResult(3, Result::Unreachable);
  r.expectResult(4, Result::Unreachable);
  r.expectResult(5, Result::Unreachable);
  r.expectNoResult();

  r.link.up = true;
  r.autoReply = true;
  const size_t before = r.link.writes.size();
  r.submit(Op::Power, 1, 6);  // reconnected, nothing heard yet
  r.expectResult(6, Result::Unreachable);
  r.advance(10000);
  TEST_ASSERT_GREATER_THAN(before, r.link.writes.size());
  for (size_t i = before; i < r.link.writes.size(); i++) TEST_ASSERT_TRUE(isPoll(r.link.writes[i]));
}

void test_queue_limit_refuses() {
  Rig r;
  r.ready();
  r.autoReply = false;
  for (uint32_t i = 0; i < ac::kMaxPending; i++) r.submit(Op::Power, 1, i);
  r.expectNoResult();
  r.submit(Op::Power, 1, 99);
  r.expectResult(99, Result::Refused);
}

void test_status_version_bumps_only_on_change() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT32(0, r.br.statusVersion());
  r.ready();
  const uint32_t v = r.br.statusVersion();
  TEST_ASSERT_GREATER_THAN_UINT32(0, v);
  r.sendStatus();
  TEST_ASSERT_EQUAL_UINT32(v, r.br.statusVersion());
  r.unit.appBit = true;  // raw byte changes, decoded fields don't
  r.sendStatus();
  TEST_ASSERT_EQUAL_UINT32(v, r.br.statusVersion());
  r.unit.air = 76;
  r.sendStatus();
  TEST_ASSERT_EQUAL_UINT32(v + 1, r.br.statusVersion());
  TEST_ASSERT_EQUAL_UINT8(76, r.br.status().airTempF);
}

void test_responding_window() {
  Rig r;
  TEST_ASSERT_FALSE(r.br.responding(r.now));
  r.ready();  // frame at 60
  r.autoReply = false;
  TEST_ASSERT_TRUE(r.br.responding(60 + 9999));
  TEST_ASSERT_FALSE(r.br.responding(60 + 10000));
  r.link.up = false;
  TEST_ASSERT_FALSE(r.br.responding(r.now));
}

void test_invalid_values_refused() {
  Rig r;
  r.unit.power = true;
  r.ready();
  r.submit(Op::Fan, 6, 1);
  r.expectResult(1, Result::Refused);
  r.submit(Op::Mode, static_cast<int>(Mode::Dehumid), 2);
  r.expectResult(2, Result::Refused);
  r.submit(Op::Light, 2, 3);
  r.expectResult(3, Result::Refused);
  r.submit(Op::StepSetpoint, 2, 4);
  r.expectResult(4, Result::Refused);
  r.advance(1000);
  TEST_ASSERT_EQUAL(0, r.commands().size());
}

void test_simple_ops_expand() {
  Rig r;
  r.unit.power = true;
  r.ready();
  r.submit(Op::Fan, 0, 1);
  r.submit(Op::Fan, 3, 2);
  r.submit(Op::Display, 0, 3);
  r.submit(Op::Oscillate, 1, 4);
  r.submit(Op::Light, 1, 5);
  r.submit(Op::Mode, static_cast<int>(Mode::FanOnly), 6);
  r.submit(Op::Celsius, 1, 7);
  r.submit(Op::Power, 0, 8);
  r.advance(6000);
  const uint8_t want[][2] = {{0x04, 0x06}, {0x04, 0x03}, {0x0A, 0x01}, {0x45, 0x01},
                             {0x1C, 0x01}, {0x02, 0x02}, {0x20, 0x01}, {0x01, 0x01}};
  auto c = r.commands();
  TEST_ASSERT_EQUAL(8, c.size());
  for (size_t i = 0; i < 8; i++) expectWrite(c[i], want[i][0], want[i][1]);
  for (uint32_t t = 1; t <= 8; t++) r.expectResult(t, Result::Ok);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_polls_every_3s_with_400ms_gap);
  RUN_TEST(test_cool_from_off_powers_on_then_mode);
  RUN_TEST(test_cool_while_eco_is_noop);
  RUN_TEST(test_preset_while_off_refused);
  RUN_TEST(test_clear_preset);
  RUN_TEST(test_step_setpoint_counts_from_pending);
  RUN_TEST(test_setpoint_refused_in_celsius_and_out_of_range);
  RUN_TEST(test_reply_differs_reports_not_confirmed_without_loop);
  RUN_TEST(test_timeout_retries_once_then_fails);
  RUN_TEST(test_unreachable_fails_fast_and_never_replays);
  RUN_TEST(test_queue_limit_refuses);
  RUN_TEST(test_status_version_bumps_only_on_change);
  RUN_TEST(test_responding_window);
  RUN_TEST(test_invalid_values_refused);
  RUN_TEST(test_simple_ops_expand);
  return UNITY_END();
}
