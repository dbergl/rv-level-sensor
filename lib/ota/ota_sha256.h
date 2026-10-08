#pragma once
#include <cstddef>
#include <cstdint>

namespace ota {

// Streaming SHA-256 (FIPS 180-4). Portable, so the device and the host tests share it.
class Sha256 {
 public:
  Sha256() { reset(); }
  void reset();
  void update(const uint8_t* data, size_t len);
  void final(uint8_t out[32]);  // ends the hash; reset() before reusing

 private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t bufLen_;
  uint64_t bits_;
};

}  // namespace ota
