#pragma once
#include <cstdint>

namespace rvc {

struct ResetDecision {
  uint8_t ackCode;        // ack::OK, or why nothing will be done
  bool reboot;            // Reboot (byte 0 bits 0-1)
  bool restoreDefaults;   // Reset to default settings
  bool openUpdateWindow;  // Reboot/Enter Bootloader (byte 1 bits 4-5) without Reboot: open the
                          // Bluetooth update window. The caller reboots instead if it has no Bluetooth.
};

// Pure decision logic for GENERAL_RESET (17F00h, spec 6.2.1). Each field is 2 bits: 01b acts,
// 00b/11b do nothing, 10b is badly formatted (NAK 4). Clear Faults succeeds without action (no
// fault latches). A request for anything unsupported (statistics, test mode, OEM settings) NAKs
// the whole command, so a frame is never half executed. Reboot wins over Enter Bootloader.
ResetDecision decideGeneralReset(const uint8_t* data, uint8_t len);

}  // namespace rvc
