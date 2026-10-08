#pragma once
#include <cstdint>

namespace rvc {

constexpr uint8_t ADDR_NULL = 0xFE;    // "no address claimed"
constexpr uint8_t ADDR_GLOBAL = 0xFF;  // broadcast destination

struct CanFrame {
  uint32_t id = 0;  // 29-bit extended identifier
  uint8_t len = 8;
  uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
};

class ITransmitter {
 public:
  virtual ~ITransmitter() = default;
  // Non-blocking. Returns false if the frame could not be queued.
  virtual bool send(const CanFrame& frame) = 0;
};

}  // namespace rvc
