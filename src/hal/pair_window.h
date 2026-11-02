#pragma once
#include <cstdint>

#include "config.h"

// The deliberate pairing window: while open, the AC unit may be bound and Home Assistant may pair.
// Closes by itself after cfg::PAIR_WINDOW_MS, or early once HA has paired.
class PairWindow {
 public:
  void open(uint32_t nowMs) {
    open_ = true;
    openedMs_ = nowMs;
  }
  void close() { open_ = false; }
  bool isOpen(uint32_t nowMs) const { return open_ && nowMs - openedMs_ < cfg::PAIR_WINDOW_MS; }

 private:
  bool open_ = false;
  uint32_t openedMs_ = 0;
};
