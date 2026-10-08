#pragma once
#include <cstdint>

#include "rvc_types.h"

namespace rvc {

struct Header {
  uint8_t priority;
  uint32_t dgn;  // 17-bit DGN as on the wire (includes DGN-Low / destination)
  uint8_t sa;
};

// DGN-High (bits 15..8) below F0h means DGN-Low carries a destination address.
inline bool isDestinationSpecific(uint32_t dgn) { return ((dgn >> 8) & 0xFF) < 0xF0; }
inline uint32_t baseDgn(uint32_t dgn) { return isDestinationSpecific(dgn) ? (dgn & 0x1FF00) : dgn; }
inline uint8_t destinationOf(uint32_t dgn) {
  return isDestinationSpecific(dgn) ? static_cast<uint8_t>(dgn & 0xFF) : ADDR_GLOBAL;
}

uint32_t makeId(uint8_t priority, uint32_t dgn, uint8_t sa);
Header parseId(uint32_t id);

}  // namespace rvc
