#include "link_policy.h"

#include <cstdio>

namespace ac {

uint32_t Backoff::next() {
  uint32_t d = nextMs_;
  nextMs_ = nextMs_ >= kMaxMs / 2 ? kMaxMs : nextMs_ * 2;
  return d;
}

void BindPicker::offer(const char* name, size_t len, const uint8_t addr[6], uint8_t type, int rssi) {
  if (name == nullptr || len < 2 || name[0] != 'K' || name[1] != 'T') return;
  if (rssi < -128) rssi = -128;
  if (rssi > 127) rssi = 127;
  // Strongest single reading wins; a tie keeps the first seen.
  if (found_ && rssi <= best_.rssi) return;
  std::memcpy(best_.addr, addr, 6);
  best_.type = type;
  best_.rssi = static_cast<int8_t>(rssi);
  size_t n = len < kNameMax ? len : kNameMax;
  std::memcpy(best_.name, name, n);
  best_.name[n] = '\0';
  found_ = true;
}

bool BindPicker::best(Candidate& out) const {
  if (!found_) return false;
  out = best_;
  return true;
}

void formatAddress(const uint8_t native[6], char out[kAddrTextLen]) {
  std::snprintf(out, kAddrTextLen, "%02X:%02X:%02X:%02X:%02X:%02X", native[5], native[4], native[3], native[2],
                native[1], native[0]);
}

}  // namespace ac
