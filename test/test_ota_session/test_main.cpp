#include <unity.h>

#include <vector>

#include "ota_session.h"
#include "ota_sha256.h"

using namespace ota;

struct FakeSink : FlashSink {
  uint32_t cap = 1000000;
  std::vector<uint8_t> data;
  int32_t beginErr = 0, writeErr = 0, finishErr = 0;
  int begins = 0, aborts = 0, finishes = 0;
  uint32_t capacity() const override { return cap; }
  int32_t begin(uint32_t) override {
    if (beginErr) return beginErr;
    ++begins;
    data.clear();
    return 0;
  }
  int32_t write(const uint8_t* p, size_t n) override {
    if (writeErr) return writeErr;
    data.insert(data.end(), p, p + n);
    return 0;
  }
  int32_t finish() override {
    ++finishes;
    return finishErr;
  }
  void abort() override { ++aborts; }
};

static FakeSink sink;

void setUp() { sink = FakeSink(); }
void tearDown() {}

static std::vector<uint8_t> image(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = uint8_t(i * 7 + 3);
  return v;
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

static std::vector<uint8_t> beginCmd(const std::vector<uint8_t>& img, bool badHash = false) {
  std::vector<uint8_t> v{op::BEGIN};
  put32(v, uint32_t(img.size()));
  uint8_t h[32];
  Sha256 s;
  s.update(img.data(), img.size());
  s.final(h);
  if (badHash) h[0] ^= 1;
  v.insert(v.end(), h, h + 32);
  return v;
}

static std::vector<uint8_t> dataCmd(uint32_t off, const std::vector<uint8_t>& img, size_t n) {
  std::vector<uint8_t> v;
  put32(v, off);
  v.insert(v.end(), img.begin() + off, img.begin() + off + n);
  return v;
}

static Notice control(Session& s, std::vector<uint8_t> cmd, uint32_t now = 0) {
  return s.onControl(cmd.data(), cmd.size(), now);
}

static Notice one(uint8_t opcode, Session& s, uint32_t now = 0) { return control(s, {opcode}, now); }

// Sends img[from, to) in `chunk`-byte writes; returns the notices produced.
static std::vector<Notice> send(Session& s, const std::vector<uint8_t>& img, size_t from, size_t to, size_t chunk,
                                uint32_t now = 0) {
  std::vector<Notice> out;
  for (size_t off = from; off < to; off += chunk) {
    size_t n = off + chunk <= to ? chunk : to - off;
    auto w = dataCmd(uint32_t(off), img, n);
    Notice nt{};
    if (s.onData(w.data(), w.size(), now, nt)) out.push_back(nt);
  }
  return out;
}

static void assertNotice(Code code, uint32_t value, const Notice& n) {
  TEST_ASSERT_EQUAL_HEX8(uint8_t(code), uint8_t(n.code));
  TEST_ASSERT_EQUAL_UINT32(value, n.value);
}

void test_pack_notice() {
  uint8_t b[5];
  packNotice({Code::Progress, 0x01020304}, b);
  const uint8_t want[5] = {0x01, 0x04, 0x03, 0x02, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b, 5);
}

void test_closed_window_rejects_everything() {
  Session s(sink);
  auto img = image(100);
  assertNotice(Code::ErrWindowClosed, 0, control(s, beginCmd(img)));
  auto w = dataCmd(0, img, 10);
  Notice n{};
  TEST_ASSERT_TRUE(s.onData(w.data(), w.size(), 0, n));
  assertNotice(Code::ErrWindowClosed, 0, n);
  TEST_ASSERT_EQUAL(0, sink.begins);
}

void test_happy_path() {
  Session s(sink);
  auto img = image(10000);
  s.openWindow(0);
  assertNotice(Code::Ok, 0, control(s, beginCmd(img)));
  TEST_ASSERT_TRUE(s.transferring());
  auto ns = send(s, img, 0, img.size(), 500);
  TEST_ASSERT_EQUAL(3, ns.size());  // crossing 4096, crossing 8192, and the last byte
  assertNotice(Code::Progress, 4500, ns[0]);
  assertNotice(Code::Progress, 8500, ns[1]);
  assertNotice(Code::Progress, 10000, ns[2]);
  assertNotice(Code::Done, 10000, one(op::FINISH, s, 1000));
  TEST_ASSERT_TRUE(sink.data == img);
  TEST_ASSERT_EQUAL(1, sink.finishes);
  TEST_ASSERT_EQUAL(0, sink.aborts);
  TEST_ASSERT_FALSE(s.rebootDue(1499));
  TEST_ASSERT_TRUE(s.rebootDue(1500));
}

// The loop's `now` can be read just before FINISH lands on the Bluetooth task, so it may be a
// little older than the DONE time: that must not look like a reboot 49 days overdue.
void test_reboot_not_due_for_a_time_before_done() {
  Session s(sink);
  auto img = image(100);
  s.openWindow(0);
  control(s, beginCmd(img));
  send(s, img, 0, 100, 100);
  assertNotice(Code::Done, 100, one(op::FINISH, s, 1000));
  TEST_ASSERT_FALSE(s.rebootDue(999));
  TEST_ASSERT_FALSE(s.rebootDue(1499));
  TEST_ASSERT_TRUE(s.rebootDue(1500));
}

void test_after_done_everything_is_refused() {
  Session s(sink);
  auto img = image(100);
  s.openWindow(0);
  control(s, beginCmd(img));
  send(s, img, 0, 100, 100);
  one(op::FINISH, s);
  assertNotice(Code::ErrWindowClosed, 0, control(s, beginCmd(img)));
  s.openWindow(10);  // ignored: the sensor is about to reboot into the new image
  assertNotice(Code::ErrWindowClosed, 0, one(op::ABORT, s, 10));
}

void test_window_expires_and_aborts() {
  Session s(sink);
  auto img = image(10000);
  s.openWindow(0);
  control(s, beginCmd(img), 1000);  // extends the deadline to 301000
  send(s, img, 0, 500, 500, 1000);
  s.tick(300999);
  TEST_ASSERT_TRUE(s.windowOpen());
  s.tick(301000);
  TEST_ASSERT_FALSE(s.windowOpen());
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_FALSE(s.transferring());
}

void test_writes_extend_window() {
  Session s(sink);
  auto img = image(10000);
  s.openWindow(0);
  control(s, beginCmd(img), 0);
  send(s, img, 0, 500, 500, 200000);  // deadline now 500000
  s.tick(400000);
  TEST_ASSERT_TRUE(s.windowOpen());
  s.tick(500000);
  TEST_ASSERT_FALSE(s.windowOpen());
}

void test_reopen_extends() {
  Session s(sink);
  s.openWindow(0);
  s.openWindow(250000);
  s.tick(300000);
  TEST_ASSERT_TRUE(s.windowOpen());
  s.tick(550000);
  TEST_ASSERT_FALSE(s.windowOpen());
}

void test_window_survives_millis_wrap() {
  Session s(sink);
  const uint32_t t0 = 0xFFFFFFF0u;
  s.openWindow(t0);
  s.tick(t0 + 100000);  // wrapped past zero
  TEST_ASSERT_TRUE(s.windowOpen());
  TEST_ASSERT_EQUAL_UINT16(200, s.secondsLeft(t0 + 100000));
  s.tick(t0 + kWindowMs);
  TEST_ASSERT_FALSE(s.windowOpen());
}

void test_seconds_left() {
  Session s(sink);
  TEST_ASSERT_EQUAL_UINT16(0, s.secondsLeft(0));
  s.openWindow(0);
  TEST_ASSERT_EQUAL_UINT16(300, s.secondsLeft(0));
  TEST_ASSERT_EQUAL_UINT16(1, s.secondsLeft(299001));
  s.tick(300000);
  TEST_ASSERT_EQUAL_UINT16(0, s.secondsLeft(300000));
}

void test_begin_size_limits() {
  Session s(sink);
  sink.cap = 1000;
  s.openWindow(0);
  assertNotice(Code::ErrBadSize, 1000, control(s, beginCmd(image(1001))));
  assertNotice(Code::ErrBadSize, 1000, control(s, beginCmd(image(0))));
  assertNotice(Code::Ok, 0, control(s, beginCmd(image(1000))));
}

void test_bad_commands() {
  Session s(sink);
  s.openWindow(0);
  auto b = beginCmd(image(10));
  b.pop_back();
  assertNotice(Code::ErrBadCommand, 0, control(s, b));      // BEGIN one byte short
  assertNotice(Code::ErrBadCommand, 0x09, one(0x09, s));    // unknown opcode
  assertNotice(Code::ErrBadCommand, 0, control(s, {}));     // empty
  assertNotice(Code::ErrBadCommand, 0, control(s, {op::FINISH, 0}));  // trailing byte
  Notice n{};
  control(s, beginCmd(image(10)));
  uint8_t shortWrite[4] = {0, 0, 0, 0};  // offset but no data
  TEST_ASSERT_TRUE(s.onData(shortWrite, 4, 0, n));
  assertNotice(Code::ErrBadCommand, 0, n);
}

void test_data_before_begin() {
  Session s(sink);
  s.openWindow(0);
  auto img = image(10);
  auto w = dataCmd(0, img, 10);
  Notice n{};
  TEST_ASSERT_TRUE(s.onData(w.data(), w.size(), 0, n));
  assertNotice(Code::ErrNotStarted, 0, n);
  assertNotice(Code::ErrNotStarted, 0, one(op::FINISH, s));
}

void test_out_of_order_single_resend_then_recovers() {
  Session s(sink);
  auto img = image(3000);
  s.openWindow(0);
  control(s, beginCmd(img));
  TEST_ASSERT_EQUAL(0, send(s, img, 0, 500, 500).size());
  auto ns = send(s, img, 1000, 2000, 500);  // 500..1000 was lost
  TEST_ASSERT_EQUAL(1, ns.size());
  assertNotice(Code::Resend, 500, ns[0]);
  TEST_ASSERT_EQUAL(0, send(s, img, 500, 1500, 500).size());  // resent, in order again
  ns = send(s, img, 2000, 2500, 500);                          // a new gap: 1500..2000
  TEST_ASSERT_EQUAL(1, ns.size());
  assertNotice(Code::Resend, 1500, ns[0]);
  ns = send(s, img, 1500, 3000, 500);
  assertNotice(Code::Progress, 3000, ns.back());
  assertNotice(Code::Done, 3000, one(op::FINISH, s));
  TEST_ASSERT_TRUE(sink.data == img);
}

void test_write_past_size_aborts() {
  Session s(sink);
  auto img = image(700);
  auto declared = image(600);
  s.openWindow(0);
  control(s, beginCmd(declared));
  send(s, img, 0, 500, 500);
  auto ns = send(s, img, 500, 700, 200);
  assertNotice(Code::ErrBadSize, 600, ns[0]);
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_FALSE(s.transferring());
}

void test_finish_early_keeps_transfer() {
  Session s(sink);
  auto img = image(1000);
  s.openWindow(0);
  control(s, beginCmd(img));
  send(s, img, 0, 600, 600);
  assertNotice(Code::ErrIncomplete, 600, one(op::FINISH, s));
  TEST_ASSERT_TRUE(s.transferring());
  send(s, img, 600, 1000, 400);
  assertNotice(Code::Done, 1000, one(op::FINISH, s));
}

void test_hash_mismatch() {
  Session s(sink);
  auto img = image(1000);
  s.openWindow(0);
  control(s, beginCmd(img, true));
  send(s, img, 0, 1000, 250);
  assertNotice(Code::ErrHash, 0, one(op::FINISH, s));
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_EQUAL(0, sink.finishes);
  TEST_ASSERT_FALSE(s.rebootDue(100000));
  TEST_ASSERT_TRUE(s.windowOpen());  // can try again
}

void test_sink_errors() {
  auto img = image(1000);
  {
    Session s(sink);
    s.openWindow(0);
    sink.beginErr = 0x1508;  // ESP_ERR_OTA_ROLLBACK_INVALID_STATE: running image still on trial
    assertNotice(Code::ErrFlash, 0x1508, control(s, beginCmd(img)));
    TEST_ASSERT_FALSE(s.transferring());
  }
  setUp();
  {
    Session s(sink);
    s.openWindow(0);
    control(s, beginCmd(img));
    sink.writeErr = 0x101;
    auto ns = send(s, img, 0, 500, 500);
    assertNotice(Code::ErrFlash, 0x101, ns[0]);
    TEST_ASSERT_EQUAL(1, sink.aborts);
  }
  setUp();
  {
    Session s(sink);
    s.openWindow(0);
    control(s, beginCmd(img));
    send(s, img, 0, 1000, 500);
    sink.finishErr = 0x1503;  // ESP_ERR_OTA_VALIDATE_FAILED
    assertNotice(Code::ErrInvalidImage, 0x1503, one(op::FINISH, s));
    TEST_ASSERT_FALSE(s.transferring());
    TEST_ASSERT_EQUAL(0, sink.aborts);  // esp_ota_end already released the handle
    TEST_ASSERT_FALSE(s.rebootDue(100000));
  }
}

void test_begin_during_transfer_restarts() {
  Session s(sink);
  auto img = image(1000);
  s.openWindow(0);
  control(s, beginCmd(img));
  send(s, img, 0, 500, 500);
  assertNotice(Code::Ok, 0, control(s, beginCmd(img)));
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_EQUAL(0, send(s, img, 0, 500, 500).size());  // offset 0 is expected again
  send(s, img, 500, 1000, 500);
  assertNotice(Code::Done, 1000, one(op::FINISH, s));
  TEST_ASSERT_TRUE(sink.data == img);
}

void test_abort_and_close() {
  Session s(sink);
  auto img = image(1000);
  s.openWindow(0);
  control(s, beginCmd(img));
  assertNotice(Code::Ok, 0, one(op::ABORT, s));
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_TRUE(s.windowOpen());
  control(s, beginCmd(img));
  assertNotice(Code::Ok, 0, one(op::CLOSE, s));
  TEST_ASSERT_EQUAL(2, sink.aborts);
  TEST_ASSERT_FALSE(s.windowOpen());
}

void test_disconnect_aborts_and_next_begin_works() {
  Session s(sink);
  auto img = image(1000);
  s.openWindow(0);
  control(s, beginCmd(img));
  send(s, img, 0, 500, 500);
  s.onDisconnect();
  TEST_ASSERT_EQUAL(1, sink.aborts);
  TEST_ASSERT_FALSE(s.transferring());
  TEST_ASSERT_TRUE(s.windowOpen());
  s.onDisconnect();  // nothing active: no second abort
  TEST_ASSERT_EQUAL(1, sink.aborts);
  assertNotice(Code::Ok, 0, control(s, beginCmd(img)));
  send(s, img, 0, 1000, 500);
  assertNotice(Code::Done, 1000, one(op::FINISH, s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pack_notice);
  RUN_TEST(test_closed_window_rejects_everything);
  RUN_TEST(test_happy_path);
  RUN_TEST(test_reboot_not_due_for_a_time_before_done);
  RUN_TEST(test_after_done_everything_is_refused);
  RUN_TEST(test_window_expires_and_aborts);
  RUN_TEST(test_writes_extend_window);
  RUN_TEST(test_reopen_extends);
  RUN_TEST(test_window_survives_millis_wrap);
  RUN_TEST(test_seconds_left);
  RUN_TEST(test_begin_size_limits);
  RUN_TEST(test_bad_commands);
  RUN_TEST(test_data_before_begin);
  RUN_TEST(test_out_of_order_single_resend_then_recovers);
  RUN_TEST(test_write_past_size_aborts);
  RUN_TEST(test_finish_early_keeps_transfer);
  RUN_TEST(test_hash_mismatch);
  RUN_TEST(test_sink_errors);
  RUN_TEST(test_begin_during_transfer_restarts);
  RUN_TEST(test_abort_and_close);
  RUN_TEST(test_disconnect_aborts_and_next_begin_works);
  return UNITY_END();
}
