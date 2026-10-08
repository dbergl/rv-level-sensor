#include "frame.h"

namespace rvc {

uint32_t makeId(uint8_t priority, uint32_t dgn, uint8_t sa) {
  return (static_cast<uint32_t>(priority & 0x7) << 26) | ((dgn & 0x1FFFF) << 8) | sa;
}

Header parseId(uint32_t id) {
  Header h;
  h.priority = static_cast<uint8_t>((id >> 26) & 0x7);
  h.dgn = (id >> 8) & 0x1FFFF;
  h.sa = static_cast<uint8_t>(id & 0xFF);
  return h;
}

}  // namespace rvc
