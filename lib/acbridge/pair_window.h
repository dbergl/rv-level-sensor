#pragma once
#include <cstdint>

namespace ac {

// The deliberate pairing window: while open, the AC unit may be bound and Home Assistant may pair.
// Closes by itself after lengthMs, or early once HA has paired. Expiry is final: the loop asks
// every pass, so a millis() wrap ~49.7 days later can't reopen it.
class PairWindow {
 public:
  explicit PairWindow(uint32_t lengthMs) : lengthMs_(lengthMs) {}

  void open(uint32_t nowMs) {
    open_ = true;
    openedMs_ = nowMs;
  }
  void close() { open_ = false; }
  bool isOpen(uint32_t nowMs) {
    if (open_ && nowMs - openedMs_ >= lengthMs_) open_ = false;
    return open_;
  }

 private:
  uint32_t lengthMs_;
  bool open_ = false;
  uint32_t openedMs_ = 0;
};

}  // namespace ac
