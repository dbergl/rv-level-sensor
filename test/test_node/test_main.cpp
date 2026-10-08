#include <unity.h>

#include <vector>

#include "../support/fake_bus.h"
#include "../support/fake_store.h"
#include "dgn.h"
#include "frame.h"
#include "node.h"

using namespace rvc;

static const uint8_t SA = 191;       // address the node ends up claiming
static const uint8_t TOOL = 0x80;    // address of the test tool sending requests/commands
static const char* kProductId = "DIY*LEVEL-MPU6050*A1B2C3D4E5F6*";

static FakeBus bus;
static FakeStore store;

void setUp() {
  bus = FakeBus();
  store = FakeStore();
}
void tearDown() {}

static NodeConfig config() {
  NodeConfig c;
  c.name = makeName(0x1234, 0);
  c.productId = kProductId;
  return c;
}

static SensorSnapshot still(float pitch, float roll) {
  SensorSnapshot s;
  s.valid = true;
  s.moving = false;
  s.rawPitchDeg = pitch;
  s.rawRollDeg = roll;
  return s;
}

// Starts claiming at t=-250 (unsigned wrap is fine), claims SA 191 at t=0, and after the
// 250 ms post-claim quiet period sends the first status + DM_RV at t=250. Then clears the bus.
static void claim(Node& n, const SensorSnapshot& s = still(0, 0)) {
  const uint32_t start = 0u - 250u;
  n.begin(0xFF, start);
  n.update(s, start);
  n.update(s, 0);
  TEST_ASSERT_TRUE(n.claimed());
  n.update(s, 250);
  bus.clear();
}

static CanFrame frame(uint8_t prio, uint32_t dgn, uint8_t sa, std::initializer_list<uint8_t> data) {
  CanFrame f;
  f.id = makeId(prio, dgn, sa);
  f.len = static_cast<uint8_t>(data.size());
  int i = 0;
  for (uint8_t b : data) f.data[i++] = b;
  return f;
}

static CanFrame request(uint8_t dest, uint32_t dgn, uint8_t instance = 0xFF) {
  return frame(6, dgn::REQUEST | dest, TOOL,
               {static_cast<uint8_t>(dgn), static_cast<uint8_t>(dgn >> 8), static_cast<uint8_t>(dgn >> 16),
                instance, 0xFF, 0xFF, 0xFF, 0xFF});
}

static CanFrame pitchCommand(uint8_t instance, uint16_t out, uint16_t within, uint8_t setZero) {
  CanFrame f;
  f.id = makeId(6, dgn::PITCH_CONFIG_COMMAND, TOOL);
  packAxisConfig({instance, out, within, setZero}, f.data);
  return f;
}

// ---- claim and startup ----

void test_only_claim_traffic_before_claimed() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  n.update(still(0, 0), 100);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  TEST_ASSERT_EQUAL_HEX32(0x18EABFFE, bus.frames[0].id);
}

void test_claim_then_250ms_quiet_then_status_and_dm() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  bus.clear();
  n.update(still(0, 0), 250);
  TEST_ASSERT_EQUAL(1, bus.frames.size());  // only ADDRESS_CLAIM (spec 00D-R04)
  TEST_ASSERT_EQUAL(1, bus.count(dgn::ADDRESS_CLAIM));
  n.update(still(0, 0), 499);
  TEST_ASSERT_EQUAL(1, bus.frames.size());
  n.update(still(0, 0), 500);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
  TEST_ASSERT_EQUAL(1, bus.count(dgn::DM_RV));
  TEST_ASSERT_EQUAL_HEX32(0x0DFFEBBF, bus.withDgn(dgn::LEVELING_SENSOR_STATUS)[0].id);
}

void test_requests_and_commands_ignored_during_quiet_period() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  n.update(still(0, 0), 250);
  bus.clear();
  n.onFrame(request(SA, dgn::DM_RV), 300);
  n.onFrame(pitchCommand(1, U16_NA, U16_NA, 1), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
  n.onFrame(request(0xFF, 0x0EE00), 300);  // claim requests are still answered
  TEST_ASSERT_EQUAL(1, bus.count(dgn::ADDRESS_CLAIM));
}

void test_claimed_address_saved_once() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0, 0), 300);
  TEST_ASSERT_EQUAL(1, store.addressSaves);
  TEST_ASSERT_EQUAL_UINT8(SA, store.address);
}

void test_lost_address_stops_traffic() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(frame(6, dgn::ADDRESS_CLAIM, SA, {0, 0, 0, 0, 0, 0, 0, 0}), 300);  // NAME 0 beats ours
  bus.clear();
  n.update(still(5, 5), 2000);
  TEST_ASSERT_FALSE(n.claimed());
  TEST_ASSERT_EQUAL(0, bus.count(dgn::LEVELING_SENSOR_STATUS));
  TEST_ASSERT_EQUAL(1, bus.count(dgn::REQUEST | 190));
}

// ---- LEVELING_SENSOR_STATUS ----

void test_status_applies_relative_zero() {
  Settings s;
  s.pitch.zeroDeg = 1.0f;
  Node n(config(), s, bus, store);
  claim(n, still(3.0f, -0.5f));
  n.update(still(3.0f, -0.5f), 1250);  // heartbeat
  auto st = bus.withDgn(dgn::LEVELING_SENSOR_STATUS);
  TEST_ASSERT_EQUAL(1, st.size());
  TEST_ASSERT_EQUAL_UINT16(encodeAngle(2.0f), get16(st[0].data + 1));
  TEST_ASSERT_EQUAL_UINT16(encodeAngle(-0.5f), get16(st[0].data + 3));
  TEST_ASSERT_EQUAL_HEX8(0x40, st[0].data[0]);  // rear low (nose high by 2 deg)
  TEST_ASSERT_EQUAL_UINT8(1, st[0].data[7]);
}

void test_status_heartbeat() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0, 0), 750);
  TEST_ASSERT_EQUAL(0, bus.count(dgn::LEVELING_SENSOR_STATUS));
  n.update(still(0, 0), 1250);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
}

void test_status_deadband_and_min_gap() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0.03f, 0), 400);  // inside deadband
  TEST_ASSERT_EQUAL(0, bus.count(dgn::LEVELING_SENSOR_STATUS));
  n.update(still(0.2f, 0), 500);  // outside deadband, gap ok
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
  n.update(still(0.4f, 0), 550);  // changed but only 50 ms later
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
  n.update(still(0.4f, 0), 600);
  TEST_ASSERT_EQUAL(2, bus.count(dgn::LEVELING_SENSOR_STATUS));
}

void test_status_sent_when_level_bits_change() {
  Settings s;
  s.pitch.outDeg = 1.0f;
  s.pitch.withinDeg = 0.5f;
  Node n(config(), s, bus, store);
  claim(n, still(-0.9f, 0));
  n.update(still(-1.1f, 0), 400);
  auto st = bus.withDgn(dgn::LEVELING_SENSOR_STATUS);
  TEST_ASSERT_EQUAL(1, st.size());
  TEST_ASSERT_EQUAL_HEX8(0x04, st[0].data[0]);  // front low
  TEST_ASSERT_TRUE(n.bits().frontLow);
}

void test_faulted_sensor_reports_error_values() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  SensorSnapshot bad;
  bad.faulted = true;
  n.update(bad, 400);
  auto st = bus.withDgn(dgn::LEVELING_SENSOR_STATUS);
  TEST_ASSERT_EQUAL(1, st.size());
  const uint8_t expected[8] = {0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, st[0].data, 8);
}

// ---- DM_RV ----

void test_dm_rv_every_5s_without_fault() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0, 0), 5249);
  TEST_ASSERT_EQUAL(0, bus.count(dgn::DM_RV));
  n.update(still(0, 0), 5250);
  auto dm = bus.withDgn(dgn::DM_RV);
  TEST_ASSERT_EQUAL(1, dm.size());
  const uint8_t expected[8] = {0x05, 81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, dm[0].data, 8);
}

void test_dm_rv_fault_immediate_then_every_second_then_clear() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  SensorSnapshot bad;
  bad.faulted = true;
  n.update(bad, 400);
  auto dm = bus.withDgn(dgn::DM_RV);
  TEST_ASSERT_EQUAL(1, dm.size());
  TEST_ASSERT_EQUAL_HEX8(0x15, dm[0].data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x4C, dm[0].data[4]);
  n.update(bad, 1399);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::DM_RV));
  n.update(bad, 1400);
  TEST_ASSERT_EQUAL(2, bus.count(dgn::DM_RV));
  n.update(still(0, 0), 1500);
  dm = bus.withDgn(dgn::DM_RV);
  TEST_ASSERT_EQUAL(3, dm.size());
  TEST_ASSERT_EQUAL_HEX8(0x05, dm[2].data[0]);
}

// ---- requests ----

void test_directed_product_id_request_sends_multipacket() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(SA, dgn::PRODUCT_ID), 300);
  for (uint32_t t = 300; t <= 600; t += 10) n.update(still(0, 0), t);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::INITIAL_PACKET | 0xFF));
  TEST_ASSERT_EQUAL(5, bus.count(dgn::DATA_PACKET | 0xFF));
}

void test_global_product_id_and_dm_requests_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, dgn::PRODUCT_ID), 300);
  n.onFrame(request(0xFF, dgn::DM_RV), 300);
  n.update(still(0, 0), 310);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
}

void test_directed_dm_rv_request() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(SA, dgn::DM_RV), 300);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::DM_RV));
}

void test_status_request_global_and_directed() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, dgn::LEVELING_SENSOR_STATUS), 300);
  n.onFrame(request(SA, dgn::LEVELING_SENSOR_STATUS, 1), 300);
  TEST_ASSERT_EQUAL(2, bus.count(dgn::LEVELING_SENSOR_STATUS));
}

void test_status_request_wrong_instance_directed_naks() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(SA, dgn::LEVELING_SENSOR_STATUS, 7), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  TEST_ASSERT_EQUAL_HEX8(ack::NAK, acks[0].data[0]);
}

void test_config_status_request_instance_zero() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(SA, dgn::ROLL_CONFIG_STATUS, 0), 300);
  auto st = bus.withDgn(dgn::ROLL_CONFIG_STATUS);
  TEST_ASSERT_EQUAL(1, st.size());
  const uint8_t expected[8] = {0x01, 0x80, 0x64, 0x40, 0x64, 0xFC, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, st[0].data, 8);
}

void test_unsupported_request_directed_naks_global_silent() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, 0x1FFFF), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  n.onFrame(request(SA, 0x1FFFF), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  const uint8_t expected[8] = {0x01, 0x01, 0xFF, 0xFF, TOOL, 0xFF, 0xFF, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, acks[0].data, 8);
  TEST_ASSERT_EQUAL_HEX8(SA, parseId(acks[0].id).sa);
}

void test_request_to_other_address_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0x42, dgn::LEVELING_SENSOR_STATUS), 300);
  n.onFrame(request(0x42, 0x1FFFF), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
}

void test_address_claim_requests_ee00_and_eeff() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, 0x0EE00), 300);
  n.onFrame(request(SA, 0x0EEFF), 300);
  TEST_ASSERT_EQUAL(2, bus.count(dgn::ADDRESS_CLAIM));
}

void test_short_request_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  CanFrame f = request(SA, 0x1FFFF);
  f.len = 2;
  n.onFrame(f, 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
}

// ---- config commands ----

void test_threshold_command_applies_saves_and_reports() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(pitchCommand(1, encodeAngle(2.0f), encodeAngle(1.0f), 3), 300);
  TEST_ASSERT_EQUAL(0, bus.count(dgn::ACK | TOOL));  // no ACK without set-zero
  TEST_ASSERT_EQUAL(1, store.settingsSaves);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, store.saved.pitch.outDeg);
  n.update(still(0, 0), 310);
  auto st = bus.withDgn(dgn::PITCH_CONFIG_STATUS);
  TEST_ASSERT_EQUAL(1, st.size());
  TEST_ASSERT_EQUAL_UINT16(encodeAngle(2.0f), get16(st[0].data + 1));
  TEST_ASSERT_EQUAL_UINT16(encodeAngle(1.0f), get16(st[0].data + 3));
}

void test_new_thresholds_change_level_bits() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(-1.5f, 0));
  TEST_ASSERT_TRUE(n.bits().frontLow);
  n.onFrame(pitchCommand(0, encodeAngle(2.0f), encodeAngle(1.8f), 0), 300);
  n.update(still(-1.5f, 0), 310);
  TEST_ASSERT_FALSE(n.bits().frontLow);
}

void test_set_zero_acks_and_rezeroes() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(1.25f, 0));
  n.onFrame(pitchCommand(1, U16_NA, U16_NA, 1), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  const uint8_t expected[8] = {0x00, 0x01, 0xFF, 0xFF, TOOL, 0xE8, 0xFD, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, acks[0].data, 8);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, store.saved.pitch.zeroDeg);
  n.update(still(1.25f, 0), 310);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, n.pitchDeg());
}

void test_set_zero_while_moving_naks_3() {
  Node n(config(), Settings{}, bus, store);
  SensorSnapshot moving = still(1.0f, 0);
  moving.moving = true;
  claim(n, moving);
  n.onFrame(pitchCommand(1, U16_NA, U16_NA, 1), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  TEST_ASSERT_EQUAL_HEX8(ack::CONDITIONS, acks[0].data[0]);
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

// zeroHere() is the serial console's set-zero: both axes, same rules as the CAN command.
void test_zero_here_zeroes_both_axes_and_saves() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(-2.4f, -0.5f));
  TEST_ASSERT_TRUE(n.zeroHere());
  TEST_ASSERT_EQUAL_FLOAT(-2.4f, store.saved.pitch.zeroDeg);
  TEST_ASSERT_EQUAL_FLOAT(-0.5f, store.saved.roll.zeroDeg);
  TEST_ASSERT_EQUAL(0, bus.count(dgn::ACK | TOOL));  // nobody on the bus asked
  n.update(still(-2.4f, -0.5f), 310);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, n.pitchDeg());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, n.rollDeg());
  TEST_ASSERT_EQUAL(1, bus.count(dgn::PITCH_CONFIG_STATUS));
  TEST_ASSERT_EQUAL(1, bus.count(dgn::ROLL_CONFIG_STATUS));
}

void test_zero_here_while_moving_is_rejected() {
  Node n(config(), Settings{}, bus, store);
  SensorSnapshot moving = still(-2.4f, -0.5f);
  moving.moving = true;
  claim(n, moving);
  TEST_ASSERT_FALSE(n.zeroHere());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, n.settings().pitch.zeroDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, n.settings().roll.zeroDeg);
}

void test_out_of_range_command_naks_5_and_keeps_settings() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(pitchCommand(1, encodeAngle(60.0f), U16_NA, 0), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  TEST_ASSERT_EQUAL_HEX8(ack::OUT_OF_RANGE, acks[0].data[0]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, n.settings().pitch.outDeg);
}

void test_roll_command_updates_roll_only() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(0, -2.0f));
  CanFrame f;
  f.id = makeId(6, dgn::ROLL_CONFIG_COMMAND, TOOL);
  packAxisConfig({1, U16_NA, U16_NA, 1}, f.data);
  n.onFrame(f, 300);
  TEST_ASSERT_EQUAL_FLOAT(-2.0f, n.settings().roll.zeroDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, n.settings().pitch.zeroDeg);
  TEST_ASSERT_EQUAL_HEX8(0xEA, bus.withDgn(dgn::ACK | TOOL)[0].data[5]);
  n.update(still(0, -2.0f), 310);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::ROLL_CONFIG_STATUS));
  TEST_ASSERT_EQUAL(0, bus.count(dgn::PITCH_CONFIG_STATUS));
}

void test_command_for_other_instance_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(pitchCommand(5, encodeAngle(60.0f), U16_NA, 1), 300);
  n.update(still(0, 0), 310);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
}

// ---- review focus ----

void test_command_before_claim_ignored() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  bus.clear();
  n.onFrame(pitchCommand(1, U16_NA, U16_NA, 1), 10);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

void test_short_command_naks_4() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  CanFrame f = pitchCommand(1, U16_NA, U16_NA, 1);
  f.len = 3;
  n.onFrame(f, 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  TEST_ASSERT_EQUAL_HEX8(ack::BAD_FORMAT, acks[0].data[0]);
}

void test_tx_failure_retries_status() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  bus.fail = true;
  n.update(still(0, 0), 1250);
  bus.fail = false;
  n.update(still(0, 0), 1260);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
}

void test_millis_wraparound() {
  Node n(config(), Settings{}, bus, store);
  const uint32_t t0 = 0xFFFFFF00u;
  n.begin(0xFF, t0);
  n.update(still(0, 0), t0);
  n.update(still(0, 0), t0 + 250);
  TEST_ASSERT_TRUE(n.claimed());
  n.update(still(0, 0), t0 + 500);  // first status
  bus.clear();
  n.update(still(0, 0), t0 + 1000);  // wrapped past zero, only 500 ms since last status
  TEST_ASSERT_EQUAL(0, bus.count(dgn::LEVELING_SENSOR_STATUS));
  n.update(still(0, 0), t0 + 1500);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::LEVELING_SENSOR_STATUS));
}

void test_invalid_persisted_settings_replaced_with_defaults() {
  Settings s;
  s.pitch = {0.5f, 2.0f, 0.0f};  // within > out
  s.roll = {1.5f, 0.75f, 0.25f};
  Node n(config(), s, bus, store);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, n.settings().pitch.outDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, n.settings().pitch.withinDeg);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, n.settings().roll.outDeg);
}

// ---- LEVEL_WHEEL_STATUS / LEVEL_CONFIG_COMMAND ----

static CanFrame blockCommand(uint8_t dest, std::initializer_list<uint8_t> data) {
  return frame(6, dgn::PROPRIETARY_A2 | dest, TOOL, data);
}

static std::vector<CanFrame> wheels() { return bus.withDgn(dgn::LEVEL_WHEEL_STATUS); }

void test_wheel_heartbeat_sends_four_records_in_order() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0, 0), 750);
  TEST_ASSERT_EQUAL(0, wheels().size());
  n.update(still(0, 0), 1250);
  auto w = wheels();
  TEST_ASSERT_EQUAL(4, w.size());
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_HEX32(0x19EFBFBF, w[i].id);
    TEST_ASSERT_EQUAL_UINT8(8, w[i].len);
    TEST_ASSERT_EQUAL_HEX8(i + 1, w[i].data[0]);
  }
}

void test_wheel_records_check_case() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(-1.0f, 0));
  n.update(still(-1.0f, 0), 1250);
  auto w = wheels();
  TEST_ASSERT_EQUAL(4, w.size());
  const uint8_t r1[8] = {0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06};
  const uint8_t r2[8] = {0x02, 0x03, 0x03, 0x00, 0x00, 0xFD, 0xFF, 0xFF};  // byte 5: tolerance auto
  const uint8_t r3[8] = {0x03, 0x1C, 0x1C, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
  const uint8_t r4[8] = {0x04, 0x36, 0x06, 0xAD, 0x02, 0x14, 0x0A, 0x32};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r1, w[0].data, 8);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r2, w[1].data, 8);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r3, w[2].data, 8);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r4, w[3].data, 8);
  TEST_ASSERT_EQUAL_UINT32(6, n.blockPlan().totalLayers);
}

void test_wheel_records_follow_relative_zero() {
  Settings s;
  s.pitch.zeroDeg = -1.0f;  // the sensor reads -1 deg where the coach is level
  Node n(config(), s, bus, store);
  claim(n, still(-1.0f, 0));
  TEST_ASSERT_TRUE(n.blockPlan().level);
}

void test_wheel_records_on_change_rate_limited() {
  Node n(config(), Settings{}, bus, store);
  claim(n);                         // heartbeat burst at 250
  n.update(still(-1.0f, 0), 400);   // changed, but only 150 ms since the last burst
  TEST_ASSERT_EQUAL(0, wheels().size());
  n.update(still(-1.0f, 0), 500);
  auto w = wheels();
  TEST_ASSERT_EQUAL(3, w.size());  // 01h-03h, no 04h
  TEST_ASSERT_EQUAL_HEX8(0x03, w[2].data[0]);
  n.update(still(0, 0), 600);
  TEST_ASSERT_EQUAL(3, wheels().size());
  n.update(still(0, 0), 750);
  TEST_ASSERT_EQUAL(6, wheels().size());
}

void test_wheel_records_not_resent_when_bytes_unchanged() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.update(still(0.01f, 0), 600);  // rounds to 0.0 deg, still level: identical records
  TEST_ASSERT_EQUAL(0, wheels().size());
}

void test_wheel_records_on_fault() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  SensorSnapshot bad;
  bad.faulted = true;
  n.update(bad, 500);
  auto w = wheels();
  TEST_ASSERT_EQUAL(3, w.size());
  const uint8_t r1[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF};
  const uint8_t r2[8] = {0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r1, w[0].data, 8);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r2, w[1].data, 8);
  n.update(bad, 1250);
  w = wheels();
  TEST_ASSERT_EQUAL(7, w.size());
  TEST_ASSERT_EQUAL_HEX8(0x04, w[6].data[0]);  // record 04h still sent normally
  TEST_ASSERT_EQUAL_HEX8(0x0A, w[6].data[6]);
}

// Starts claiming at a preferred address in range (the sensor's range is B0h-BFh).
static void claimAt(Node& n, uint8_t sa) {
  const uint32_t start = 0u - 250u;
  n.begin(sa, start);
  n.update(still(0, 0), start);
  n.update(still(0, 0), 0);
  TEST_ASSERT_EQUAL_HEX8(sa, n.address());
  n.update(still(0, 0), 250);
  bus.clear();
}

// The panel subscribes by the full DGN, destination included, so status always goes to BFh.
void test_wheel_status_dgn_low_stays_bf_at_another_address() {
  Node n(config(), Settings{}, bus, store);
  claimAt(n, 0xB5);
  n.update(still(1, 0), 1300);
  auto w = wheels();
  TEST_ASSERT_TRUE(w.size() >= 4);
  for (const auto& f : w) TEST_ASSERT_EQUAL_HEX32(0x19EFBFB5, f.id);
  TEST_ASSERT_EQUAL(0, bus.count(0x0FFB0));
}

void test_wheel_request_for_1ef00_or_1efbf() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, 0x1EF00), 300);  // J1939 style: PDU1 PGN with a 00h low byte
  TEST_ASSERT_EQUAL(4, wheels().size());
  n.onFrame(request(SA, 0x1EFBF), 300);
  TEST_ASSERT_EQUAL(8, wheels().size());
  TEST_ASSERT_EQUAL(0, bus.count(dgn::ACK | TOOL));
}

void test_wheel_request_for_old_0ffb0_is_nakked() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(SA, 0x0FFB0), 300);
  TEST_ASSERT_EQUAL(0, wheels().size());
  TEST_ASSERT_EQUAL_HEX8(ack::NAK, bus.withDgn(dgn::ACK | TOOL)[0].data[0]);
}

// The panel's fixed frames go to BFh even if the sensor has had to claim another address.
void test_block_command_to_bf_accepted_at_another_address() {
  Node n(config(), Settings{}, bus, store);
  claimAt(n, 0xB5);
  n.onFrame(blockCommand(0xBF, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL(11, n.settings().blocks.blocksCarried);
  n.onFrame(blockCommand(0xB5, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL(12, n.settings().blocks.blocksCarried);
  bus.clear();
  n.onFrame(blockCommand(0x65, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);  // the heater's
  TEST_ASSERT_EQUAL(12, n.settings().blocks.blocksCarried);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
}

void test_wheel_request_global_and_directed() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(request(0xFF, dgn::LEVEL_WHEEL_STATUS), 300);
  TEST_ASSERT_EQUAL(4, wheels().size());
  n.onFrame(request(SA, dgn::LEVEL_WHEEL_STATUS, 7), 300);  // instance ignored
  TEST_ASSERT_EQUAL(8, wheels().size());
  TEST_ASSERT_EQUAL(0, bus.count(dgn::ACK | TOOL));
}

// Request §5 example frame, sent to global as the panel does.
void test_block_command_global_applies_saves_and_echoes() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(-1.0f, 0));
  n.onFrame(blockCommand(0xFF, {0x4C, 0x56, 0x02, 0x14, 0x0C, 0x19, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL(1, store.settingsSaves);
  TEST_ASSERT_EQUAL_UINT8(12, store.saved.blocks.blocksCarried);
  TEST_ASSERT_EQUAL_UINT8(12, n.settings().blocks.blocksCarried);
  auto w = wheels();
  TEST_ASSERT_EQUAL(4, w.size());  // 04h echo first, then 01h-03h
  const uint8_t r4[8] = {0x04, 0x36, 0x06, 0xAD, 0x02, 0x14, 0x0C, 0x19};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r4, w[0].data, 8);
  TEST_ASSERT_EQUAL_HEX8(0x01, w[1].data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, w[3].data[0]);
}

// Request §6 test 3: fewer blocks than layers sets "short on blocks" straight away.
void test_block_command_recomputes_plan() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(-1.0f, 0));
  n.onFrame(blockCommand(0xFF, {0x4C, 0x56, 0x02, 0xFF, 0x05, 0xFF, 0xFF, 0xFF}), 300);
  auto w = wheels();
  TEST_ASSERT_EQUAL(4, w.size());
  TEST_ASSERT_EQUAL_HEX8(0x04, w[1].data[5]);  // record 01h byte 5: short on blocks
  TEST_ASSERT_TRUE(n.blockPlan().shortOnBlocks);
}

void test_block_command_directed_to_us_applies() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(SA, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL_UINT8(11, n.settings().blocks.blocksCarried);
}

void test_carried_plus_minus_steps_by_one() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  const std::initializer_list<uint8_t> up = {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const std::initializer_list<uint8_t> down = {0x4C, 0x56, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  n.onFrame(blockCommand(0xFF, up), 300);
  n.onFrame(blockCommand(0xFF, up), 310);
  n.onFrame(blockCommand(0xFF, down), 320);
  TEST_ASSERT_EQUAL_UINT8(11, n.settings().blocks.blocksCarried);
  TEST_ASSERT_EQUAL(3, store.settingsSaves);
  auto w = wheels();
  TEST_ASSERT_EQUAL_HEX8(0x04, w[8].data[0]);
  TEST_ASSERT_EQUAL_HEX8(11, w[8].data[6]);
}

void test_block_command_rejected_echoes_unchanged_config() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(0xFF, {0x4C, 0x56, 0x01, 0x50, 0xC3, 0xFF, 0xFF, 0xFF}), 300);  // 5000.0 in
  auto w = wheels();
  TEST_ASSERT_EQUAL(1, w.size());  // record 04h only
  const uint8_t r4[8] = {0x04, 0x36, 0x06, 0xAD, 0x02, 0x14, 0x0A, 0x32};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(r4, w[0].data, 8);
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

void test_block_command_without_signature_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(0xFF, {0xAA, 0x55, 0x02, 0x14, 0x0C, 0x19, 0xFF, 0xFF}), 300);  // BMS heater
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

// LEVEL_CONFIG_COMMAND moved to proprietary-A2 1EF00h; a valid LV frame on 0EF00h is someone
// else's traffic now.
void test_block_command_on_proprietary_a_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(frame(6, 0x0EF00 | SA, TOOL, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  n.onFrame(frame(6, 0x0EF00 | 0xFF, TOOL, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

// LEVEL SETUP page: a tolerance step fixes the value (flag 00b), the AUTO op restores auto (01b).
void test_tolerance_steppers_update_config_and_auto_flag() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(0xBF, {0x4C, 0x56, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);  // +0.05 from auto 0.50
  TEST_ASSERT_EQUAL_HEX8(0x37, wheels()[0].data[7]);  // record 04h: 0.55 in
  TEST_ASSERT_EQUAL_HEX8(0xFC, bus.withDgn(dgn::LEVEL_WHEEL_STATUS)[2].data[5]);  // record 02h: fixed
  bus.clear();
  n.onFrame(blockCommand(0xBF, {0x4C, 0x56, 0x0A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 400);
  TEST_ASSERT_EQUAL_HEX8(0x32, wheels()[0].data[7]);
  TEST_ASSERT_EQUAL_HEX8(0xFD, wheels()[2].data[5]);  // auto again
  TEST_ASSERT_EQUAL(2, store.settingsSaves);
}

// LEVEL SETUP page: the geometry steppers change record 04h's wheelbase and track and are saved.
void test_geometry_steppers_update_config() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(0xBF, {0x4C, 0x56, 0x0B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);  // wheelbase +0.5
  const uint8_t wb[4] = {0x3B, 0x06, 0xAD, 0x02};  // 159.5 in, 68.5 in
  TEST_ASSERT_EQUAL_HEX8_ARRAY(wb, wheels()[0].data + 1, 4);
  bus.clear();
  n.onFrame(blockCommand(0xBF, {0x4C, 0x56, 0x0E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 400);  // track -0.5
  const uint8_t tr[4] = {0x3B, 0x06, 0xA8, 0x02};  // 159.5 in, 68.0 in
  TEST_ASSERT_EQUAL_HEX8_ARRAY(tr, wheels()[0].data + 1, 4);
  TEST_ASSERT_EQUAL(2, store.settingsSaves);
}

void test_block_command_to_other_address_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(blockCommand(0x42, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL_UINT8(10, n.settings().blocks.blocksCarried);
}

void test_block_command_before_claim_ignored() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  bus.clear();
  n.onFrame(blockCommand(0xFF, {0x4C, 0x56, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), 10);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
}

void test_wheel_heartbeat_retries_after_tx_failure() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  bus.fail = true;
  n.update(still(0, 0), 1250);
  bus.fail = false;
  n.update(still(0, 0), 1260);
  TEST_ASSERT_EQUAL(4, wheels().size());
}

void test_invalid_persisted_block_config_replaced() {
  Settings s;
  s.blocks.blockHeight = 0;  // would divide by zero
  s.blocks.blocksCarried = 99;
  Node n(config(), s, bus, store);
  TEST_ASSERT_TRUE(n.settings().blocks == level::BlockConfig{});
}

// Sensor noise around a rounding edge must not make the panel flicker: records 01h-03h
// follow a displayed angle that only moves when the live angle leaves a deadband.
void test_wheel_records_ignore_jitter_inside_deadband() {
  Node n(config(), Settings{}, bus, store);
  claim(n, still(0.15f, 0));
  const float jitter[] = {0.13f, 0.17f, 0.12f, 0.18f, 0.14f, 0.16f};
  uint32_t t = 500;
  for (float p : jitter) {
    n.update(still(p, 0), t);
    t += 100;
  }
  TEST_ASSERT_EQUAL(0, wheels().size());
  n.update(still(0.36f, 0), t);  // a real move
  auto w = wheels();
  TEST_ASSERT_EQUAL(3, w.size());
  TEST_ASSERT_EQUAL_UINT16(4, w[0].data[1] | w[0].data[2] << 8);
}

// ---- GENERAL_RESET ----

static CanFrame generalReset(uint8_t dest, uint8_t b0, uint8_t b1) {
  return frame(6, dgn::GENERAL_RESET | dest, TOOL, {b0, b1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
}

void test_general_reset_reboot_acks_then_reboots_after_delay() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(generalReset(SA, 0xFD, 0xFF), 300);
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  const uint8_t expected[8] = {0x00, 0x01, 0xFF, 0xFF, TOOL, 0x00, 0x7F, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, acks[0].data, 8);
  TEST_ASSERT_FALSE(n.rebootDue(300));  // let the ACK leave the TX queue first
  TEST_ASSERT_FALSE(n.rebootDue(549));
  TEST_ASSERT_TRUE(n.rebootDue(550));
}

void test_no_reboot_without_request() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  TEST_ASSERT_FALSE(n.rebootDue(100000));
}

void test_general_reset_restores_every_default_and_reports() {
  Settings s;
  s.pitch = {2.0f, 1.0f, 1.5f};
  s.roll.zeroDeg = -0.5f;
  s.blocks.blocksCarried = 3;
  Node n(config(), s, bus, store);
  claim(n, still(1.5f, -0.5f));
  n.onFrame(generalReset(SA, 0xDF, 0xFF), 300);
  TEST_ASSERT_EQUAL(1, store.settingsSaves);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, store.saved.pitch.outDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, store.saved.pitch.withinDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, store.saved.pitch.zeroDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, store.saved.roll.zeroDeg);
  TEST_ASSERT_TRUE(store.saved.blocks == level::BlockConfig{});
  TEST_ASSERT_EQUAL_HEX8(ack::OK, bus.withDgn(dgn::ACK | TOOL)[0].data[0]);
  auto w = wheels();
  TEST_ASSERT_EQUAL(1, w.size());  // record 04h reports the default block config
  TEST_ASSERT_EQUAL_HEX8(0x0A, w[0].data[6]);
  n.update(still(1.5f, -0.5f), 310);
  TEST_ASSERT_EQUAL(1, bus.count(dgn::PITCH_CONFIG_STATUS));
  TEST_ASSERT_EQUAL(1, bus.count(dgn::ROLL_CONFIG_STATUS));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, n.pitchDeg());  // zero offset gone
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_to_global_or_other_address_ignored() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(generalReset(0xFF, 0xDD, 0xFF), 300);
  n.onFrame(generalReset(0x42, 0xDD, 0xFF), 300);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_unsupported_flag_naks_and_does_nothing() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(generalReset(SA, 0x5D, 0xFF), 300);  // statistics + defaults + reboot
  auto acks = bus.withDgn(dgn::ACK | TOOL);
  TEST_ASSERT_EQUAL(1, acks.size());
  TEST_ASSERT_EQUAL_HEX8(ack::NAK, acks[0].data[0]);
  TEST_ASSERT_EQUAL(0, store.settingsSaves);
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_bad_format_naks_4() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(generalReset(SA, 0xFE, 0xFF), 300);
  TEST_ASSERT_EQUAL_HEX8(ack::BAD_FORMAT, bus.withDgn(dgn::ACK | TOOL)[0].data[0]);
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_before_claim_ignored() {
  Node n(config(), Settings{}, bus, store);
  n.begin(0xFF, 0);
  n.update(still(0, 0), 0);
  bus.clear();
  n.onFrame(generalReset(SA, 0xFD, 0xFF), 10);
  TEST_ASSERT_EQUAL(0, bus.frames.size());
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_bootloader_opens_window_when_bluetooth_up() {
  Node n(config(), Settings{}, bus, store);
  n.setUpdateWindowSupported(true);
  claim(n);
  n.onFrame(generalReset(SA, 0xFF, 0xDF), 300);
  TEST_ASSERT_EQUAL_HEX8(ack::OK, bus.withDgn(dgn::ACK | TOOL)[0].data[0]);
  TEST_ASSERT_TRUE(n.takeUpdateWindowRequest());
  TEST_ASSERT_FALSE(n.takeUpdateWindowRequest());  // taken once
  TEST_ASSERT_FALSE(n.rebootDue(10000));
}

void test_general_reset_bootloader_reboots_without_bluetooth() {
  Node n(config(), Settings{}, bus, store);
  claim(n);
  n.onFrame(generalReset(SA, 0xFF, 0xDF), 300);
  TEST_ASSERT_EQUAL_HEX8(ack::OK, bus.withDgn(dgn::ACK | TOOL)[0].data[0]);
  TEST_ASSERT_FALSE(n.takeUpdateWindowRequest());
  TEST_ASSERT_TRUE(n.rebootDue(550));
}

void test_general_reset_reboot_and_bootloader_reboots() {
  Node n(config(), Settings{}, bus, store);
  n.setUpdateWindowSupported(true);
  claim(n);
  n.onFrame(generalReset(SA, 0xFD, 0xDF), 300);
  TEST_ASSERT_FALSE(n.takeUpdateWindowRequest());
  TEST_ASSERT_TRUE(n.rebootDue(550));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_only_claim_traffic_before_claimed);
  RUN_TEST(test_claim_then_250ms_quiet_then_status_and_dm);
  RUN_TEST(test_requests_and_commands_ignored_during_quiet_period);
  RUN_TEST(test_claimed_address_saved_once);
  RUN_TEST(test_lost_address_stops_traffic);
  RUN_TEST(test_status_applies_relative_zero);
  RUN_TEST(test_status_heartbeat);
  RUN_TEST(test_status_deadband_and_min_gap);
  RUN_TEST(test_status_sent_when_level_bits_change);
  RUN_TEST(test_faulted_sensor_reports_error_values);
  RUN_TEST(test_dm_rv_every_5s_without_fault);
  RUN_TEST(test_dm_rv_fault_immediate_then_every_second_then_clear);
  RUN_TEST(test_directed_product_id_request_sends_multipacket);
  RUN_TEST(test_global_product_id_and_dm_requests_ignored);
  RUN_TEST(test_directed_dm_rv_request);
  RUN_TEST(test_status_request_global_and_directed);
  RUN_TEST(test_status_request_wrong_instance_directed_naks);
  RUN_TEST(test_config_status_request_instance_zero);
  RUN_TEST(test_unsupported_request_directed_naks_global_silent);
  RUN_TEST(test_request_to_other_address_ignored);
  RUN_TEST(test_address_claim_requests_ee00_and_eeff);
  RUN_TEST(test_short_request_ignored);
  RUN_TEST(test_threshold_command_applies_saves_and_reports);
  RUN_TEST(test_new_thresholds_change_level_bits);
  RUN_TEST(test_set_zero_acks_and_rezeroes);
  RUN_TEST(test_set_zero_while_moving_naks_3);
  RUN_TEST(test_zero_here_zeroes_both_axes_and_saves);
  RUN_TEST(test_zero_here_while_moving_is_rejected);
  RUN_TEST(test_out_of_range_command_naks_5_and_keeps_settings);
  RUN_TEST(test_roll_command_updates_roll_only);
  RUN_TEST(test_command_for_other_instance_ignored);
  RUN_TEST(test_command_before_claim_ignored);
  RUN_TEST(test_short_command_naks_4);
  RUN_TEST(test_tx_failure_retries_status);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_invalid_persisted_settings_replaced_with_defaults);
  RUN_TEST(test_wheel_heartbeat_sends_four_records_in_order);
  RUN_TEST(test_wheel_records_check_case);
  RUN_TEST(test_wheel_records_follow_relative_zero);
  RUN_TEST(test_wheel_records_on_change_rate_limited);
  RUN_TEST(test_wheel_records_not_resent_when_bytes_unchanged);
  RUN_TEST(test_wheel_records_on_fault);
  RUN_TEST(test_wheel_request_global_and_directed);
  RUN_TEST(test_wheel_status_dgn_low_stays_bf_at_another_address);
  RUN_TEST(test_wheel_request_for_1ef00_or_1efbf);
  RUN_TEST(test_wheel_request_for_old_0ffb0_is_nakked);
  RUN_TEST(test_block_command_to_bf_accepted_at_another_address);
  RUN_TEST(test_block_command_global_applies_saves_and_echoes);
  RUN_TEST(test_block_command_recomputes_plan);
  RUN_TEST(test_block_command_directed_to_us_applies);
  RUN_TEST(test_carried_plus_minus_steps_by_one);
  RUN_TEST(test_block_command_rejected_echoes_unchanged_config);
  RUN_TEST(test_block_command_without_signature_ignored);
  RUN_TEST(test_block_command_on_proprietary_a_ignored);
  RUN_TEST(test_tolerance_steppers_update_config_and_auto_flag);
  RUN_TEST(test_geometry_steppers_update_config);
  RUN_TEST(test_block_command_to_other_address_ignored);
  RUN_TEST(test_block_command_before_claim_ignored);
  RUN_TEST(test_wheel_heartbeat_retries_after_tx_failure);
  RUN_TEST(test_invalid_persisted_block_config_replaced);
  RUN_TEST(test_wheel_records_ignore_jitter_inside_deadband);
  RUN_TEST(test_general_reset_reboot_acks_then_reboots_after_delay);
  RUN_TEST(test_no_reboot_without_request);
  RUN_TEST(test_general_reset_restores_every_default_and_reports);
  RUN_TEST(test_general_reset_to_global_or_other_address_ignored);
  RUN_TEST(test_general_reset_unsupported_flag_naks_and_does_nothing);
  RUN_TEST(test_general_reset_bad_format_naks_4);
  RUN_TEST(test_general_reset_before_claim_ignored);
  RUN_TEST(test_general_reset_bootloader_opens_window_when_bluetooth_up);
  RUN_TEST(test_general_reset_bootloader_reboots_without_bluetooth);
  RUN_TEST(test_general_reset_reboot_and_bootloader_reboots);
  return UNITY_END();
}
