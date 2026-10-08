#pragma once
#include <cstdint>

#include "address_claim.h"
#include "axis_config.h"
#include "block_config.h"
#include "blocks.h"
#include "codec.h"
#include "general_reset.h"
#include "level_logic.h"
#include "multipacket.h"
#include "rvc_types.h"
#include "wheel_codec.h"

namespace rvc {

struct NodeConfig {
  uint64_t name = 0;
  uint8_t saHigh = 191;
  uint8_t saLow = 176;
  uint8_t instance = 1;
  uint8_t dsa = 81;
  const char* productId = "***";
  uint32_t statusMinGapMs = 100;
  uint32_t statusHeartbeatMs = 1000;
  float statusDeadbandDeg = 0.05f;
  uint32_t configStatusMinGapMs = 100;
  uint32_t dmRvOkIntervalMs = 5000;
  uint32_t dmRvFaultIntervalMs = 1000;
  uint32_t wheelHeartbeatMs = 1000;  // LEVEL_WHEEL_STATUS: all four records
  uint32_t wheelMinGapMs = 250;      // on-change bursts of records 01h-03h
  float wheelDeadbandDeg = 0.05f;     // displayed angle moves only beyond this, so noise does not flicker
  uint32_t rebootDelayMs = 250;       // GENERAL_RESET: time for the ACK to leave the TX queue
  void (*log)(const char* line) = nullptr;  // optional diagnostics sink
};

struct SensorSnapshot {
  bool faulted = false;  // IMU not responding
  bool valid = false;    // angles usable (not faulted and filter seeded)
  bool moving = true;
  float rawPitchDeg = 0;  // filtered, before relative zero
  float rawRollDeg = 0;
};

class ISettingsStore {
 public:
  virtual ~ISettingsStore() = default;
  virtual void saveSettings(const Settings& s) = 0;
  virtual void saveAddress(uint8_t sa) = 0;
};

// The RV-C leveling sensor node: address claim, requests, status broadcasts, DM_RV,
// pitch/roll configuration commands, and the proprietary block-leveling messages.
class Node {
 public:
  static constexpr uint32_t kSpnAdc = 2;         // Table 7.3: node analog-digital converter
  static constexpr uint8_t kFmiBadDevice = 12;   // Table 3.2.5.8: bad intelligent device

  Node(const NodeConfig& cfg, const Settings& initial, ITransmitter& tx, ISettingsStore& store);

  void begin(uint8_t lastSa, uint32_t nowMs);
  void restartClaim(uint32_t nowMs);
  void update(const SensorSnapshot& s, uint32_t nowMs);
  void onFrame(const CanFrame& f, uint32_t nowMs);
  // Sets pitch and roll zero at the current reading, as the CAN set-zero command does (for the
  // serial console). Returns false, changing nothing, when the reading is invalid or moving.
  bool zeroHere();
  // True once a GENERAL_RESET reboot was ACKed and its delay has passed; the caller restarts the chip.
  bool rebootDue(uint32_t nowMs) const { return rebootPending_ && nowMs - rebootRequestMs_ >= cfg_.rebootDelayMs; }
  // Whether a GENERAL_RESET Reboot/Enter Bootloader can open the Bluetooth update window. False
  // (the default, and when Bluetooth failed to start): that request reboots instead.
  void setUpdateWindowSupported(bool supported) { updateWindowSupported_ = supported; }
  // True once after an accepted request to open the update window.
  bool takeUpdateWindowRequest() {
    bool r = updateWindowRequested_;
    updateWindowRequested_ = false;
    return r;
  }

  bool claimed() const { return claimer_.claimed(); }
  uint8_t address() const { return claimer_.address(); }
  float pitchDeg() const { return pitch_; }
  float rollDeg() const { return roll_; }
  level::LevelBits bits() const { return bits_; }
  const Settings& settings() const { return settings_; }
  const level::BlockPlan& blockPlan() const { return plan_; }

 private:
  enum AxisIndex { kPitch = 0, kRoll = 1 };

  void handleRequest(uint8_t requester, uint8_t dest, const RequestMsg& req, uint32_t nowMs);
  void handleConfigCommand(int axis, uint8_t sender, const CanFrame& f);
  ConfigResult applyConfig(int axis, const AxisConfigMsg& cmd, const char* from);
  void maybeSendStatus(uint32_t nowMs);
  void maybeSendDmRv(uint32_t nowMs);
  void maybeSendConfigStatus(int axis, uint32_t nowMs);
  bool sendStatus(uint32_t nowMs);
  bool sendDmRv(uint32_t nowMs);
  bool sendConfigStatus(int axis, uint32_t nowMs);
  void sendAck(uint8_t code, uint8_t dest, uint32_t ackedDgn);
  void sendProductId();
  void handleBlockCommand(const CanFrame& f, uint32_t nowMs);
  void handleGeneralReset(uint8_t sender, const CanFrame& f, uint32_t nowMs);
  void maybeSendWheelStatus(uint32_t nowMs);
  void packWheelStatus(uint8_t out[3][8]) const;
  bool sendWheelStatus(uint32_t nowMs);  // records 01h-03h
  bool sendWheelConfig();                // record 04h
  bool sendWheelRecord(const uint8_t data[8]);
  void applyThresholds();
  void log(const char* fmt, ...);

  NodeConfig cfg_;
  Settings settings_;
  ITransmitter& tx_;
  ISettingsStore& store_;
  AddressClaimer claimer_;
  MultiPacketTx mp_;
  level::LevelLogic logic_;

  SensorSnapshot snap_;
  float pitch_ = 0;
  float roll_ = 0;
  level::LevelBits bits_;
  level::BlockPlan plan_;  // from the displayed angles below
  bool wheelSeeded_ = false;
  float wheelPitch_ = 0;  // angles shown on LEVEL_WHEEL_STATUS
  float wheelRoll_ = 0;

  bool wasClaimed_ = false;
  uint8_t savedSa_ = ADDR_NULL;

  bool statusSent_ = false;
  uint32_t lastStatusMs_ = 0;
  bool lastValid_ = false;
  level::LevelBits lastBits_;
  float lastPitch_ = 0;
  float lastRoll_ = 0;

  bool dmSent_ = false;
  bool lastDmFault_ = false;
  uint32_t lastDmMs_ = 0;

  bool cfgPending_[2] = {false, false};
  bool cfgSent_[2] = {false, false};
  uint32_t lastCfgMs_[2] = {0, 0};

  bool wheelSent_ = false;
  uint32_t lastWheelBeatMs_ = 0;  // last full four-record heartbeat
  uint32_t lastWheelMs_ = 0;      // last burst of records 01h-03h, for the on-change gap
  uint8_t lastWheel_[3][8] = {};  // records 01h-03h as last sent

  bool rebootPending_ = false;
  uint32_t rebootRequestMs_ = 0;
  bool updateWindowSupported_ = false;
  bool updateWindowRequested_ = false;
};

}  // namespace rvc
