#include <unity.h>

#include <cstdio>
#include <cstring>

#include "ha_control.h"

using namespace ac;
using Auth = HaControl::Auth;
using Out = HaControl::Out;

// From `uv run tools/ac/vectors.py`.
static const char* kHaPubHex = "8f40c5adb68f25624ae5b214ea767a6ec94d829d3d7b5e1ad1ba6f3e2138285f";
static const char* kEspPrivHex = "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f";
static const char* kEspPubHex = "358072d6365880d1aeea329adf9121383851ed21a28e3b75e965d0d2cd166254";
static const char* kPairKeyHex = "c727107d2c582dd9c86f513150a56d1043ed571501b60a279fda3df22d108c45";
static const char* kNonceHex = "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
static const char* kMsgHex = "01000000020036e461c8a09b15adb32d84cfb276225c";

static void hexTo(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = uint8_t(v);
  }
}

static uint8_t haPub[32], espPriv[32], espPub[32], pairKey[32], nonce[16], vecMsg[22];

// The "random" source: espPriv for a pairing, then nonces counting up from the vector's.
static uint8_t nonceBump = 0;
static void fakeFill(uint8_t* out, size_t n) {
  if (n == 32) {
    memcpy(out, espPriv, 32);
  } else {
    memcpy(out, nonce, n);
    out[0] = uint8_t(out[0] + nonceBump++);
  }
}

namespace {

constexpr uint16_t kHa = 1;
constexpr uint16_t kOther = 2;

struct FakeLink : IUnitLink {
  bool up = false;
  bool connected() const override { return up; }
  bool write(const uint8_t*, size_t) override { return true; }
};

// A valid unit status frame: power per the flag, cool, 70 °F.
void frame(bool power, uint8_t f[cm::kFrameLen]) {
  memset(f, 0, cm::kFrameLen);
  f[0] = f[1] = 0x5A;
  f[2] = uint8_t((power ? 0x80 : 0) | 0x01);
  f[4] = 75;
  f[8] = 52;
  f[9] = 0x2C;
  f[16] = 1;
  f[17] = 70;
  uint8_t s = 0;
  for (int i = 0; i < 18; i++) s = uint8_t(s + f[i]);
  f[18] = s;
  f[19] = 0x0D;
  f[20] = 0x0A;
}

struct Rig {
  uint32_t now = 1000;
  FakeLink link;
  Bridge br{link};
  HaControl ha{br, fakeFill};
  Out out;

  void pairDirect() { ha.setKey(pairKey); }
  Auth auth(uint16_t h, const uint8_t* d, size_t n, bool open = false) { return ha.onAuth(h, d, n, open, out); }
  // Starts a session on h; returns the nonce it got.
  void session(uint16_t h, uint8_t got[16]) {
    const uint8_t m[] = {HaControl::kSession};
    TEST_ASSERT_EQUAL(Auth::Session, auth(h, m, 1));
    TEST_ASSERT_EQUAL_size_t(17, out.n);
    TEST_ASSERT_EQUAL_UINT16(h, out.h);
    memcpy(got, out.d + 1, 16);
  }
  // counter || op || value || mac under the given nonce.
  size_t cmd(const uint8_t n16[16], uint32_t counter, uint8_t op, int8_t value, uint8_t msg[22]) {
    for (int i = 0; i < 4; ++i) msg[i] = uint8_t(counter >> (8 * i));
    msg[4] = op;
    msg[5] = uint8_t(value);
    commandMac(pairKey, n16, counter, msg + 4, 2, msg + 6);
    return 22;
  }
  void command(uint16_t h, const uint8_t* m, size_t n) { ha.onCommand(h, m, n, now, out); }
  void expectReply(uint16_t h, uint32_t counter, Result r) {
    uint8_t want[kResultLen];
    packResult(counter, r, want);
    TEST_ASSERT_EQUAL_size_t(kResultLen, out.n);
    TEST_ASSERT_EQUAL_UINT16(h, out.h);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out.d, kResultLen);
  }
  void expectResult(uint16_t h, uint32_t counter, Result r) {
    TEST_ASSERT_TRUE_MESSAGE(ha.nextResult(out), "no result");
    expectReply(h, counter, r);
  }
  void makeResponding() {
    link.up = true;
    br.tick(now);
    uint8_t f[cm::kFrameLen];
    frame(false, f);
    br.onNotify(f, sizeof f, now);
    TEST_ASSERT_TRUE(br.responding(now));
  }
};

}  // namespace

void setUp() {
  hexTo(kHaPubHex, haPub, 32);
  hexTo(kEspPrivHex, espPriv, 32);
  hexTo(kEspPubHex, espPub, 32);
  hexTo(kPairKeyHex, pairKey, 32);
  hexTo(kNonceHex, nonce, 16);
  hexTo(kMsgHex, vecMsg, 22);
  nonceBump = 0;
}
void tearDown() {}

static void test_pairing_refused_while_window_closed() {
  Rig r;
  uint8_t m[33] = {HaControl::kPair};
  memcpy(m + 1, haPub, 32);
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, m, sizeof m, false));
  TEST_ASSERT_EQUAL_size_t(1, r.out.n);
  TEST_ASSERT_EQUAL_HEX8(0x01, r.out.d[0]);
  TEST_ASSERT_FALSE(r.ha.paired());
}

static void test_pairing_matches_vectors() {
  Rig r;
  uint8_t m[33] = {HaControl::kPair};
  memcpy(m + 1, haPub, 32);
  TEST_ASSERT_EQUAL(Auth::Paired, r.auth(kHa, m, sizeof m, true));
  TEST_ASSERT_EQUAL_size_t(33, r.out.n);
  TEST_ASSERT_EQUAL_HEX8(0x01, r.out.d[0]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(espPub, r.out.d + 1, 32);
  TEST_ASSERT_TRUE(r.ha.paired());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pairKey, r.ha.key(), 32);
}

static void test_pairing_refuses_low_order_and_bad_length() {
  Rig r;
  uint8_t m[33] = {HaControl::kPair};  // all-zero peer key: low order
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, m, sizeof m, true));
  TEST_ASSERT_EQUAL_size_t(1, r.out.n);
  memcpy(m + 1, haPub, 32);
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, m, 32, true));
  TEST_ASSERT_EQUAL_size_t(1, r.out.n);
  TEST_ASSERT_FALSE(r.ha.paired());
}

static void test_session_refused_without_key() {
  Rig r;
  const uint8_t m[] = {HaControl::kSession};
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, m, 1));
  TEST_ASSERT_EQUAL_size_t(1, r.out.n);
  TEST_ASSERT_EQUAL_HEX8(0x02, r.out.d[0]);
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.sessionLink());
  r.command(kHa, vecMsg, sizeof vecMsg);
  r.expectReply(kHa, 1, Result::BadAuth);
}

static void test_unknown_auth_write_ignored() {
  Rig r;
  r.pairDirect();
  const uint8_t m[] = {0x03};
  TEST_ASSERT_EQUAL(Auth::None, r.auth(kHa, m, 1));
  TEST_ASSERT_EQUAL_size_t(0, r.out.n);
  const uint8_t s[] = {HaControl::kSession, 0x00};  // trailing byte
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, s, 2));
  TEST_ASSERT_EQUAL_size_t(1, r.out.n);
}

// The vector message is accepted under the vector nonce and goes to the bridge, tagged so its
// result comes back with HA's counter (Unreachable here: no unit).
static void test_vector_command_accepted_and_result_routed() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16];
  r.session(kHa, n16);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(nonce, n16, 16);
  r.command(kHa, vecMsg, sizeof vecMsg);
  TEST_ASSERT_EQUAL_size_t(0, r.out.n);
  r.expectResult(kHa, 1, Result::Unreachable);
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));
}

static void test_replay_gets_bad_auth() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16];
  r.session(kHa, n16);
  r.command(kHa, vecMsg, sizeof vecMsg);
  r.expectResult(kHa, 1, Result::Unreachable);
  r.command(kHa, vecMsg, sizeof vecMsg);
  r.expectReply(kHa, 1, Result::BadAuth);
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));  // nothing submitted
}

static void test_replay_into_a_new_session_gets_bad_auth() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16];
  r.session(kHa, n16);
  r.command(kHa, vecMsg, sizeof vecMsg);
  r.expectResult(kHa, 1, Result::Unreachable);
  r.ha.onDisconnect(kHa);
  r.session(kHa, n16);  // fresh nonce
  r.command(kHa, vecMsg, sizeof vecMsg);
  r.expectReply(kHa, 1, Result::BadAuth);
}

static void test_bad_mac_and_short_message_get_bad_auth() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  r.cmd(n16, 7, uint8_t(Op::Power), 1, m);
  m[21] ^= 1;
  r.command(kHa, m, sizeof m);
  r.expectReply(kHa, 7, Result::BadAuth);
  r.command(kHa, m, 3);
  r.expectReply(kHa, 0, Result::BadAuth);
}

static void test_unknown_op_gets_bad_auth() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  r.cmd(n16, 1, 10, 0, m);
  r.command(kHa, m, sizeof m);
  r.expectReply(kHa, 1, Result::BadAuth);
}

static void test_command_from_another_link_gets_bad_auth() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  r.cmd(n16, 1, uint8_t(Op::Power), 1, m);
  r.command(kOther, m, sizeof m);
  r.expectReply(kOther, 1, Result::BadAuth);
  r.command(kHa, m, sizeof m);  // still fresh for the session's own link
  TEST_ASSERT_EQUAL_size_t(0, r.out.n);
  r.expectResult(kHa, 1, Result::Unreachable);
}

static void test_new_session_on_same_link_restarts_counter_and_nonce() {
  Rig r;
  r.pairDirect();
  uint8_t a[16], b[16], m[22];
  r.session(kHa, a);
  r.cmd(a, 5, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectResult(kHa, 5, Result::Unreachable);
  r.session(kHa, b);
  TEST_ASSERT_FALSE(memcmp(a, b, 16) == 0);
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.takeDrop());
  r.cmd(b, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectResult(kHa, 1, Result::Unreachable);
}

static void test_session_from_another_link_replaces_and_drops_the_old() {
  Rig r;
  r.pairDirect();
  uint8_t a[16], b[16], m[22];
  r.session(kHa, a);
  r.session(kOther, b);
  TEST_ASSERT_EQUAL_UINT16(kOther, r.ha.sessionLink());
  TEST_ASSERT_EQUAL_UINT16(kHa, r.ha.takeDrop());
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.takeDrop());
  r.cmd(a, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectReply(kHa, 1, Result::BadAuth);
}

static void test_disconnect_ends_session() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  r.ha.onDisconnect(kOther);
  TEST_ASSERT_EQUAL_UINT16(kHa, r.ha.sessionLink());
  r.ha.onDisconnect(kHa);
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.sessionLink());
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now, r.out));
  r.cmd(n16, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);  // the handle came back as a new link
  r.expectReply(kHa, 1, Result::BadAuth);
}

static void test_forget_clears_key_and_drops_session() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  r.ha.forget();
  TEST_ASSERT_FALSE(r.ha.paired());
  TEST_ASSERT_EQUAL_UINT16(kHa, r.ha.takeDrop());
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.sessionLink());
  r.cmd(n16, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectReply(kHa, 1, Result::BadAuth);
  const uint8_t s[] = {HaControl::kSession};
  TEST_ASSERT_EQUAL(Auth::Refused, r.auth(kHa, s, 1));
}

static void test_repairing_replaces_key_and_ends_session() {
  Rig r;
  r.ha.setKey(espPriv);  // some other key
  uint8_t n16[16];
  r.session(kHa, n16);
  uint8_t m[33] = {HaControl::kPair};
  memcpy(m + 1, haPub, 32);
  TEST_ASSERT_EQUAL(Auth::Paired, r.auth(kOther, m, sizeof m, true));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pairKey, r.ha.key(), 32);
  TEST_ASSERT_EQUAL_UINT16(HaControl::kNoLink, r.ha.sessionLink());
  TEST_ASSERT_EQUAL_UINT16(kHa, r.ha.takeDrop());
}

static void test_panel_results_discarded() {
  Rig r;
  r.br.submit(Intent{Op::Power, 1, 0}, r.now);
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));
}

// A command still in the bridge when its session ends: the result is dropped, and a new session
// reusing the same counter does not get it.
static void test_result_of_ended_session_dropped() {
  Rig r;
  r.pairDirect();
  r.makeResponding();
  uint8_t a[16], b[16], m[22];
  r.session(kHa, a);
  r.cmd(a, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));  // queued in the bridge
  r.ha.onDisconnect(kHa);
  r.session(kHa, b);
  r.link.up = false;
  r.br.tick(r.now);  // the outage fails the old intent
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));
  r.cmd(b, 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectResult(kHa, 1, Result::Unreachable);
}

// Results the bridge dropped (nobody drained) must not wedge the in-flight table.
static void test_lost_results_do_not_wedge() {
  Rig r;
  r.pairDirect();
  uint8_t n16[16], m[22];
  r.session(kHa, n16);
  const uint32_t total = HaControl::kMaxInFlight + 1;
  for (uint32_t c = 1; c <= total; ++c) {
    r.cmd(n16, c, uint8_t(Op::Power), 1, m);
    r.command(kHa, m, sizeof m);
  }
  for (uint32_t c = total - kMaxResults + 1; c <= total; ++c) r.expectResult(kHa, c, Result::Unreachable);
  TEST_ASSERT_FALSE(r.ha.nextResult(r.out));
  r.cmd(n16, total + 1, uint8_t(Op::Power), 1, m);
  r.command(kHa, m, sizeof m);
  r.expectResult(kHa, total + 1, Result::Unreachable);
}

static void test_status_schedule() {
  Rig r;
  r.pairDirect();
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now, r.out));  // no session
  uint8_t n16[16];
  r.session(kHa, n16);
  TEST_ASSERT_TRUE(r.ha.statusDue(r.now, r.out));  // right away
  TEST_ASSERT_EQUAL_size_t(kStatusLen, r.out.n);
  TEST_ASSERT_EQUAL_UINT16(kHa, r.out.h);
  TEST_ASSERT_EQUAL_HEX8(kStatusVersion, r.out.d[0]);
  TEST_ASSERT_EQUAL_HEX8(0, r.out.d[1] & 0x01);
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now + 100, r.out));

  r.makeResponding();  // responding and the decoded status both change
  TEST_ASSERT_TRUE(r.ha.statusDue(r.now, r.out));
  TEST_ASSERT_EQUAL_HEX8(0x01, r.out.d[1] & 0x01);
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now + 10, r.out));

  uint8_t f[cm::kFrameLen];
  frame(true, f);
  r.br.onNotify(f, sizeof f, r.now + 20);
  TEST_ASSERT_TRUE(r.ha.statusDue(r.now + 20, r.out));  // version changed
  TEST_ASSERT_EQUAL_HEX8(0x03, r.out.d[1] & 0x03);
  frame(true, f);
  r.br.onNotify(f, sizeof f, r.now + 30);  // same status again
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now + 30, r.out));
  TEST_ASSERT_FALSE(r.ha.statusDue(r.now + 20 + 4999, r.out));
  TEST_ASSERT_TRUE(r.ha.statusDue(r.now + 20 + 5000, r.out));  // heartbeat
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pairing_refused_while_window_closed);
  RUN_TEST(test_pairing_matches_vectors);
  RUN_TEST(test_pairing_refuses_low_order_and_bad_length);
  RUN_TEST(test_session_refused_without_key);
  RUN_TEST(test_unknown_auth_write_ignored);
  RUN_TEST(test_vector_command_accepted_and_result_routed);
  RUN_TEST(test_replay_gets_bad_auth);
  RUN_TEST(test_replay_into_a_new_session_gets_bad_auth);
  RUN_TEST(test_bad_mac_and_short_message_get_bad_auth);
  RUN_TEST(test_unknown_op_gets_bad_auth);
  RUN_TEST(test_command_from_another_link_gets_bad_auth);
  RUN_TEST(test_new_session_on_same_link_restarts_counter_and_nonce);
  RUN_TEST(test_session_from_another_link_replaces_and_drops_the_old);
  RUN_TEST(test_disconnect_ends_session);
  RUN_TEST(test_forget_clears_key_and_drops_session);
  RUN_TEST(test_repairing_replaces_key_and_ends_session);
  RUN_TEST(test_panel_results_discarded);
  RUN_TEST(test_result_of_ended_session_dropped);
  RUN_TEST(test_lost_results_do_not_wedge);
  RUN_TEST(test_status_schedule);
  return UNITY_END();
}
