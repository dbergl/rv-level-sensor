#include "general_reset.h"

#include "axis_config.h"

namespace rvc {

ResetDecision decideGeneralReset(const uint8_t* d, uint8_t len) {
  if (len < 2) return {ack::BAD_FORMAT, false, false, false};
  auto field = [d](int byte, int shift) { return (d[byte] >> shift) & 0x3; };
  bool reserved = false;
  for (int byte = 0; byte < 2; ++byte)
    for (int shift = 0; shift < 8; shift += 2) reserved = reserved || field(byte, shift) == 2;
  if (reserved) return {ack::BAD_FORMAT, false, false, false};

  bool resetStatistics = field(0, 6) == 1;
  bool testMode = field(1, 0) == 1;
  bool oemReset = field(1, 2) == 1;
  bool oemSave = field(1, 6) == 1;
  if (resetStatistics || testMode || oemReset || oemSave) return {ack::NAK, false, false, false};

  bool reboot = field(0, 0) == 1;
  bool enterBootloader = field(1, 4) == 1;
  return {ack::OK, reboot, field(0, 4) == 1, enterBootloader && !reboot};
}

}  // namespace rvc
