#include "ota_session.h"

#include <cstring>

namespace ota {
namespace {

uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

void packNotice(const Notice& n, uint8_t out[5]) {
  out[0] = static_cast<uint8_t>(n.code);
  for (int i = 0; i < 4; ++i) out[1 + i] = static_cast<uint8_t>(n.value >> (8 * i));
}

void Session::openWindow(uint32_t nowMs) {
  if (done_) return;
  open_ = true;
  extend(nowMs);
}

void Session::tick(uint32_t nowMs) {
  if (open_ && static_cast<int32_t>(deadlineMs_ - nowMs) <= 0) {
    abortTransfer();
    open_ = false;
  }
}

uint16_t Session::secondsLeft(uint32_t nowMs) const {
  if (!open_) return 0;
  int32_t ms = static_cast<int32_t>(deadlineMs_ - nowMs);
  return ms <= 0 ? 0 : static_cast<uint16_t>((ms + 999) / 1000);
}

void Session::abortTransfer() {
  if (active_) sink_.abort();
  active_ = false;
}

void Session::onDisconnect() { abortTransfer(); }

Notice Session::onControl(const uint8_t* d, size_t len, uint32_t nowMs) {
  if (!open_ || done_) return {Code::ErrWindowClosed, 0};
  if (len == 0) return {Code::ErrBadCommand, 0};
  extend(nowMs);
  switch (d[0]) {
    case op::BEGIN:
      return begin(d, len);
    case op::FINISH:
      return len == 1 ? finish(nowMs) : Notice{Code::ErrBadCommand, 0};
    case op::ABORT:
      if (len != 1) return {Code::ErrBadCommand, 0};
      abortTransfer();
      return {Code::Ok, 0};
    case op::CLOSE:
      if (len != 1) return {Code::ErrBadCommand, 0};
      abortTransfer();
      open_ = false;
      return {Code::Ok, 0};
    default:
      return {Code::ErrBadCommand, d[0]};
  }
}

Notice Session::begin(const uint8_t* d, size_t len) {
  if (len != 37) return {Code::ErrBadCommand, 0};
  abortTransfer();
  uint32_t size = le32(d + 1);
  if (size == 0 || size > sink_.capacity()) return {Code::ErrBadSize, sink_.capacity()};
  int32_t err = sink_.begin(size);
  if (err) return {Code::ErrFlash, static_cast<uint32_t>(err)};
  active_ = true;
  size_ = size;
  received_ = 0;
  resendSent_ = false;
  std::memcpy(expected_, d + 5, 32);
  hash_.reset();
  return {Code::Ok, 0};
}

bool Session::onData(const uint8_t* d, size_t len, uint32_t nowMs, Notice& out) {
  if (!open_ || done_) {
    out = {Code::ErrWindowClosed, 0};
    return true;
  }
  if (!active_) {
    out = {Code::ErrNotStarted, 0};
    return true;
  }
  if (len < 5) {
    out = {Code::ErrBadCommand, 0};
    return true;
  }
  extend(nowMs);
  uint32_t offset = le32(d);
  const uint8_t* p = d + 4;
  size_t n = len - 4;
  if (offset != received_) {
    if (resendSent_) return false;
    resendSent_ = true;
    out = {Code::Resend, received_};
    return true;
  }
  resendSent_ = false;
  if (n > size_ - received_) {
    abortTransfer();
    out = {Code::ErrBadSize, size_};
    return true;
  }
  int32_t err = sink_.write(p, n);
  if (err) {
    abortTransfer();
    out = {Code::ErrFlash, static_cast<uint32_t>(err)};
    return true;
  }
  hash_.update(p, n);
  uint32_t before = received_;
  received_ += static_cast<uint32_t>(n);
  if (received_ / kProgressEvery != before / kProgressEvery || received_ == size_) {
    out = {Code::Progress, received_};
    return true;
  }
  return false;
}

Notice Session::finish(uint32_t nowMs) {
  if (!active_) return {Code::ErrNotStarted, 0};
  if (received_ != size_) return {Code::ErrIncomplete, received_};
  uint8_t got[32];
  hash_.final(got);
  if (std::memcmp(got, expected_, 32) != 0) {
    abortTransfer();
    return {Code::ErrHash, 0};
  }
  int32_t err = sink_.finish();
  active_ = false;  // finish() released the transfer either way
  if (err) return {Code::ErrInvalidImage, static_cast<uint32_t>(err)};
  done_ = true;
  doneMs_ = nowMs;
  return {Code::Done, size_};
}

}  // namespace ota
