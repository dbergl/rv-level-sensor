#include "node.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "dgn.h"
#include "frame.h"

namespace rvc {

Node::Node(const NodeConfig& cfg, const Settings& initial, ITransmitter& tx, ISettingsStore& store)
    : cfg_(cfg),
      settings_(initial),
      tx_(tx),
      store_(store),
      claimer_(cfg.name, cfg.saHigh, cfg.saLow) {
  if (!isValidAxisSettings(settings_.pitch)) settings_.pitch = AxisSettings{};
  if (!isValidAxisSettings(settings_.roll)) settings_.roll = AxisSettings{};
  if (!level::isValidBlockConfig(settings_.blocks)) settings_.blocks = level::BlockConfig{};
  applyThresholds();
}

void Node::begin(uint8_t lastSa, uint32_t nowMs) {
  savedSa_ = lastSa;
  claimer_.begin(lastSa, nowMs);
}

void Node::restartClaim(uint32_t nowMs) {
  mp_.cancel();
  wasClaimed_ = false;
  claimer_.begin(claimer_.claimed() ? claimer_.address() : savedSa_, nowMs);
}

void Node::applyThresholds() {
  logic_.setPitchThresholds(settings_.pitch.outDeg, settings_.pitch.withinDeg);
  logic_.setRollThresholds(settings_.roll.outDeg, settings_.roll.withinDeg);
}

void Node::log(const char* fmt, ...) {
  if (!cfg_.log) return;
  char line[96];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  cfg_.log(line);
}

void Node::update(const SensorSnapshot& s, uint32_t nowMs) {
  snap_ = s;
  pitch_ = s.rawPitchDeg - settings_.pitch.zeroDeg;
  roll_ = s.rawRollDeg - settings_.roll.zeroDeg;
  if (s.valid) {
    bits_ = logic_.update(pitch_, roll_);
    if (!wheelSeeded_ || std::fabs(pitch_ - wheelPitch_) > cfg_.wheelDeadbandDeg ||
        std::fabs(roll_ - wheelRoll_) > cfg_.wheelDeadbandDeg) {
      wheelSeeded_ = true;
      wheelPitch_ = pitch_;
      wheelRoll_ = roll_;
    }
    plan_ = level::planBlocks(wheelPitch_, wheelRoll_, settings_.blocks);
  } else {
    logic_.reset();
    bits_ = level::LevelBits{};
    wheelSeeded_ = false;
    plan_ = level::BlockPlan{};
  }

  claimer_.tick(nowMs, tx_);
  if (!claimer_.claimed()) {
    if (wasClaimed_) log("lost address, reclaiming");
    wasClaimed_ = false;
    mp_.cancel();
    return;
  }
  if (!wasClaimed_) {
    wasClaimed_ = true;
    statusSent_ = false;
    dmSent_ = false;
    wheelSent_ = false;
    if (claimer_.address() != savedSa_) {
      savedSa_ = claimer_.address();
      store_.saveAddress(savedSa_);
    }
    log("claimed address %u", claimer_.address());
  }
  if (!claimer_.ready(nowMs)) return;

  mp_.poll(nowMs, claimer_.address(), tx_);
  maybeSendStatus(nowMs);
  maybeSendDmRv(nowMs);
  maybeSendConfigStatus(kPitch, nowMs);
  maybeSendConfigStatus(kRoll, nowMs);
  maybeSendWheelStatus(nowMs);
}

void Node::onFrame(const CanFrame& f, uint32_t nowMs) {
  Header h = parseId(f.id);
  uint32_t base = baseDgn(h.dgn);
  uint8_t dest = destinationOf(h.dgn);

  if (base == dgn::ADDRESS_CLAIM) {
    if (f.len == 8) claimer_.onAddressClaim(h.sa, unpackName(f.data), nowMs, tx_);
    return;
  }
  if (base == dgn::REQUEST) {
    if (f.len >= 3) handleRequest(h.sa, dest, unpackRequest(f.data), nowMs);
    return;
  }
  if (!claimer_.ready(nowMs)) return;
  if (base == dgn::GENERAL_RESET) {
    if (dest == claimer_.address()) handleGeneralReset(h.sa, f, nowMs);  // spec 6.2.1: directed only
    return;
  }
  if (base == dgn::PROPRIETARY_A2) {
    if (dest == ADDR_GLOBAL || dest == claimer_.address() || dest == dgn::LEVEL_FIXED_DA) handleBlockCommand(f, nowMs);
    return;
  }
  if (h.dgn == dgn::PITCH_CONFIG_COMMAND) handleConfigCommand(kPitch, h.sa, f);
  if (h.dgn == dgn::ROLL_CONFIG_COMMAND) handleConfigCommand(kRoll, h.sa, f);
}

void Node::handleRequest(uint8_t requester, uint8_t dest, const RequestMsg& req, uint32_t nowMs) {
  if ((req.dgn & 0x1FF00) == dgn::ADDRESS_CLAIM) {  // EE00h, or EEFFh from J1939 tools
    claimer_.onRequestForClaim(dest, tx_);
    return;
  }
  if (!claimer_.ready(nowMs)) return;
  bool directed = dest != ADDR_GLOBAL;
  if (directed && dest != claimer_.address()) return;
  log("request %05lX from %u%s", static_cast<unsigned long>(req.dgn), requester, directed ? "" : " (global)");

  bool anyInstance = req.instance == 0xFF || req.instance == cfg_.instance;
  switch (req.dgn) {
    case dgn::PRODUCT_ID:
      if (directed) sendProductId();
      return;
    case dgn::DM_RV:
      if (directed) sendDmRv(nowMs);
      return;
    case dgn::LEVELING_SENSOR_STATUS:
      if (anyInstance) {
        sendStatus(nowMs);
        return;
      }
      break;
    case dgn::PITCH_CONFIG_STATUS:
      if (anyInstance || req.instance == 0) {
        sendConfigStatus(kPitch, nowMs);
        return;
      }
      break;
    case dgn::ROLL_CONFIG_STATUS:
      if (anyInstance || req.instance == 0) {
        sendConfigStatus(kRoll, nowMs);
        return;
      }
      break;
    case dgn::PROPRIETARY_A2:      // J1939 writes a PDU1 PGN with a 00h low byte
    case dgn::LEVEL_WHEEL_STATUS:  // global or directed, any instance
      sendWheelStatus(nowMs);
      sendWheelConfig();
      return;
    default:
      break;
  }
  if (directed) sendAck(ack::NAK, requester, req.dgn);
}

void Node::handleConfigCommand(int axis, uint8_t sender, const CanFrame& f) {
  uint32_t cmdDgn = axis == kPitch ? dgn::PITCH_CONFIG_COMMAND : dgn::ROLL_CONFIG_COMMAND;
  if (f.len >= 1 && f.data[0] != 0 && f.data[0] != cfg_.instance) return;
  if (f.len < 6) {
    sendAck(ack::BAD_FORMAT, sender, cmdDgn);
    return;
  }
  char from[8];
  snprintf(from, sizeof(from), "%u", sender);
  ConfigResult r = applyConfig(axis, unpackAxisConfig(f.data), from);
  if (r.sendAck) sendAck(r.ackCode, sender, cmdDgn);
}

bool Node::zeroHere() {
  if (!snap_.valid || snap_.moving) return false;  // checked up front so both axes change or neither
  const AxisConfigMsg setZero{cfg_.instance, U16_NA, U16_NA, 1};
  bool pitchOk = applyConfig(kPitch, setZero, "serial").outcome == ConfigOutcome::Applied;
  bool rollOk = applyConfig(kRoll, setZero, "serial").outcome == ConfigOutcome::Applied;
  return pitchOk && rollOk;
}

ConfigResult Node::applyConfig(int axis, const AxisConfigMsg& cmd, const char* from) {
  AxisSettings& cur = axis == kPitch ? settings_.pitch : settings_.roll;
  AxisReading reading{snap_.valid, snap_.moving, axis == kPitch ? snap_.rawPitchDeg : snap_.rawRollDeg};
  ConfigResult r = applyAxisConfigCommand(cmd, cur, cfg_.instance, reading);
  if (r.outcome == ConfigOutcome::Ignored) return r;
  log("%s config from %s -> %s", axis == kPitch ? "pitch" : "roll", from,
      r.outcome == ConfigOutcome::Applied ? "applied" : "rejected");

  if (r.outcome == ConfigOutcome::Applied) {
    bool changed = r.next.outDeg != cur.outDeg || r.next.withinDeg != cur.withinDeg ||
                   r.next.zeroDeg != cur.zeroDeg;
    cur = r.next;
    applyThresholds();
    if (changed) store_.saveSettings(settings_);
    cfgPending_[axis] = true;
  }
  return r;
}

void Node::maybeSendStatus(uint32_t nowMs) {
  uint32_t since = nowMs - lastStatusMs_;
  bool due = !statusSent_ || since >= cfg_.statusHeartbeatMs;
  if (!due && since >= cfg_.statusMinGapMs) {
    due = snap_.valid != lastValid_ || bits_ != lastBits_ ||
          (snap_.valid && (std::fabs(pitch_ - lastPitch_) > cfg_.statusDeadbandDeg ||
                           std::fabs(roll_ - lastRoll_) > cfg_.statusDeadbandDeg));
  }
  if (due) sendStatus(nowMs);
}

bool Node::sendStatus(uint32_t nowMs) {
  CanFrame f;
  f.id = makeId(prio::SENSOR_STATUS, dgn::LEVELING_SENSOR_STATUS, claimer_.address());
  packSensorStatus({snap_.valid, bits_, pitch_, roll_, cfg_.instance}, f.data);
  if (!tx_.send(f)) return false;
  statusSent_ = true;
  lastStatusMs_ = nowMs;
  lastValid_ = snap_.valid;
  lastBits_ = bits_;
  lastPitch_ = pitch_;
  lastRoll_ = roll_;
  return true;
}

void Node::maybeSendDmRv(uint32_t nowMs) {
  bool fault = snap_.faulted;
  uint32_t interval = fault ? cfg_.dmRvFaultIntervalMs : cfg_.dmRvOkIntervalMs;
  if (!dmSent_ || fault != lastDmFault_ || nowMs - lastDmMs_ >= interval) sendDmRv(nowMs);
}

bool Node::sendDmRv(uint32_t nowMs) {
  CanFrame f;
  f.id = makeId(prio::NORMAL, dgn::DM_RV, claimer_.address());
  packDmRv({cfg_.dsa, snap_.faulted, kSpnAdc, kFmiBadDevice}, f.data);
  if (!tx_.send(f)) return false;
  dmSent_ = true;
  lastDmFault_ = snap_.faulted;
  lastDmMs_ = nowMs;
  return true;
}

void Node::maybeSendConfigStatus(int axis, uint32_t nowMs) {
  if (!cfgPending_[axis]) return;
  if (cfgSent_[axis] && nowMs - lastCfgMs_[axis] < cfg_.configStatusMinGapMs) return;
  sendConfigStatus(axis, nowMs);
}

bool Node::sendConfigStatus(int axis, uint32_t nowMs) {
  const AxisSettings& s = axis == kPitch ? settings_.pitch : settings_.roll;
  CanFrame f;
  f.id = makeId(prio::NORMAL, axis == kPitch ? dgn::PITCH_CONFIG_STATUS : dgn::ROLL_CONFIG_STATUS,
                claimer_.address());
  packAxisConfig({cfg_.instance, encodeAngle(s.outDeg), encodeAngle(s.withinDeg), 0}, f.data);
  if (!tx_.send(f)) return false;
  cfgPending_[axis] = false;
  cfgSent_[axis] = true;
  lastCfgMs_[axis] = nowMs;
  return true;
}

void Node::handleBlockCommand(const CanFrame& f, uint32_t nowMs) {
  BlockCmdResult r = applyBlockConfigCommand(f.data, f.len, settings_.blocks);
  if (r.outcome == BlockCmdOutcome::Ignored) return;
  bool applied = r.outcome == BlockCmdOutcome::Applied;
  log("block config op %02X from %u -> %s", f.data[2], parseId(f.id).sa, applied ? "applied" : "rejected");
  if (applied && r.next != settings_.blocks) {
    settings_.blocks = r.next;
    store_.saveSettings(settings_);
    if (snap_.valid) plan_ = level::planBlocks(wheelPitch_, wheelRoll_, settings_.blocks);
  }
  // The record 04h echo is the acknowledgement, whether the command was applied or rejected.
  sendWheelConfig();
  if (applied) sendWheelStatus(nowMs);
}

void Node::handleGeneralReset(uint8_t sender, const CanFrame& f, uint32_t nowMs) {
  ResetDecision r = decideGeneralReset(f.data, f.len);
  bool window = r.openUpdateWindow && updateWindowSupported_;
  bool reboot = r.reboot || (r.openUpdateWindow && !updateWindowSupported_);
  log("general reset from %u: reboot=%d defaults=%d window=%d -> ack %u", sender, reboot, r.restoreDefaults, window,
      r.ackCode);
  if (r.ackCode == ack::OK && r.restoreDefaults) {
    settings_ = Settings{};
    applyThresholds();
    store_.saveSettings(settings_);
    wheelSeeded_ = false;
    cfgPending_[kPitch] = cfgPending_[kRoll] = true;  // spec 6.2.1: report the configuration
    sendWheelConfig();
  }
  sendAck(r.ackCode, sender, dgn::GENERAL_RESET);
  if (r.ackCode != ack::OK) return;
  if (reboot) {
    rebootPending_ = true;
    rebootRequestMs_ = nowMs;
  } else if (window) {
    updateWindowRequested_ = true;
  }
}

void Node::maybeSendWheelStatus(uint32_t nowMs) {
  if (!wheelSent_ || nowMs - lastWheelBeatMs_ >= cfg_.wheelHeartbeatMs) {
    if (sendWheelStatus(nowMs) && sendWheelConfig()) {
      wheelSent_ = true;
      lastWheelBeatMs_ = nowMs;
    }
    return;
  }
  if (nowMs - lastWheelMs_ < cfg_.wheelMinGapMs) return;
  uint8_t recs[3][8];
  packWheelStatus(recs);
  if (std::memcmp(recs, lastWheel_, sizeof(recs)) != 0) sendWheelStatus(nowMs);
}

void Node::packWheelStatus(uint8_t out[3][8]) const {
  const WheelStatusMsg m{snap_.valid, snap_.moving, wheelPitch_, wheelRoll_, plan_,
                         settings_.blocks.toleranceHundredthIn == level::kToleranceAuto};
  packWheelAttitude(m, out[0]);
  packWheelLayers(m, out[1]);
  packWheelLift(m, out[2]);
}

bool Node::sendWheelStatus(uint32_t nowMs) {
  uint8_t recs[3][8];
  packWheelStatus(recs);
  for (const auto& rec : recs)
    if (!sendWheelRecord(rec)) return false;
  std::memcpy(lastWheel_, recs, sizeof(recs));
  lastWheelMs_ = nowMs;
  return true;
}

bool Node::sendWheelConfig() {
  uint8_t rec[8];
  packWheelConfig(settings_.blocks, rec);
  return sendWheelRecord(rec);
}

bool Node::sendWheelRecord(const uint8_t data[8]) {
  CanFrame f;
  f.id = makeId(prio::NORMAL, dgn::LEVEL_WHEEL_STATUS, claimer_.address());
  std::memcpy(f.data, data, 8);
  return tx_.send(f);
}

void Node::sendAck(uint8_t code, uint8_t dest, uint32_t ackedDgn) {
  CanFrame f;
  f.id = makeId(prio::NORMAL, dgn::ACK | dest, claimer_.address());
  packAck(code, cfg_.instance, dest, ackedDgn, f.data);
  tx_.send(f);
  log("ack code %u to %u for %05lX", code, dest, static_cast<unsigned long>(ackedDgn));
}

void Node::sendProductId() {
  size_t len = std::strlen(cfg_.productId);
  if (len <= 8) {
    CanFrame f;
    f.id = makeId(prio::NORMAL, dgn::PRODUCT_ID, claimer_.address());
    std::memcpy(f.data, cfg_.productId, len);
    tx_.send(f);
    return;
  }
  mp_.start(dgn::PRODUCT_ID, reinterpret_cast<const uint8_t*>(cfg_.productId), static_cast<uint16_t>(len));
}

}  // namespace rvc
