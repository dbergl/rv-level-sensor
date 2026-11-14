#pragma once
#include <cstdint>

#include "bridge.h"
#include "frame.h"
#include "rvc_types.h"

// Instance-7 RV-C air conditioner for the Firefly panel: panel commands become bridge intents,
// bridge status becomes 1FFE2, 1FFE1, 1FF9C, 1FEFA and 0FFA1. Silent while the unit isn't
// answering, so the panel goes stale instead of showing a guess.
namespace rvc {

class AcPanel {
 public:
  static constexpr uint32_t kHeartbeatMs = 5000;
  static constexpr uint32_t kRetryMs = 500;  // back-off after a full TX queue
  static constexpr uint32_t kTagPanel = 0;  // results for these intents are discarded

  AcPanel(ac::Bridge& b, ITransmitter& tx, uint8_t instance = 7)
      : bridge_(b), tx_(tx), instance_(instance) {}

  void onFrame(const CanFrame& f, uint32_t nowMs);
  void update(bool claimed, uint8_t sa, uint32_t nowMs);

 private:
  void submit(ac::Op op, int v, uint32_t nowMs);
  void onCommand1(const uint8_t* d, uint32_t nowMs);
  void onCommand2(const uint8_t* d, uint32_t nowMs);
  void onOptions(const uint8_t* d, uint32_t nowMs);
  void onAcCommand(const uint8_t* d, uint32_t nowMs);
  void setRecirculate(bool on);
  void sendStatus(uint8_t sa, uint32_t nowMs);
  bool sendFrame(uint32_t dgn, uint8_t sa, const uint8_t d[8]);

  ac::Bridge& bridge_;
  ITransmitter& tx_;
  uint8_t instance_;
  uint8_t ownSa_ = ADDR_NULL;
  bool sent_ = false;  // reset while silent so the first frame after a gap goes out at once
  uint32_t sentVersion_ = 0;
  // Recirculate has no unit command (the hardware has no fresh-air damper): the panel's toggle
  // is kept here and shown back, and goes back to on whenever the AC is off.
  bool recirculate_ = true;
  uint32_t localVersion_ = 0;  // bumps when recirculate_ changes
  uint32_t sentLocal_ = 0;
  uint32_t sentMs_ = 0;
  uint8_t pending_ = 0;  // bit per status DGN still to send; only the failed ones are retried
  uint32_t retryAt_ = 0;
};

}  // namespace rvc
