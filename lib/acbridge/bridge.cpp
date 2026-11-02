#include "bridge.h"

namespace ac {

namespace {

constexpr uint8_t kUnknown = 0xFF;

bool isBool(int8_t v) { return v == 0 || v == 1; }

// Cooling, with or without a preset.
bool cooling(cm::Mode m) {
  return m == cm::Mode::Cool || m == cm::Mode::Eco || m == cm::Mode::Sleep || m == cm::Mode::Turbo;
}

bool isPreset(cm::Mode m) { return m == cm::Mode::Eco || m == cm::Mode::Sleep || m == cm::Mode::Turbo; }

uint8_t modeData(cm::Mode m) {
  switch (m) {
    case cm::Mode::Eco: return cm::kModeEco;
    case cm::Mode::Sleep: return cm::kModeSleep;
    case cm::Mode::Turbo: return cm::kModeTurbo;
    case cm::Mode::FanOnly: return cm::kModeFanOnly;
    default: return cm::kModeCool;
  }
}

// raw is left out: it carries bits nobody decodes (e.g. the vendor-app flag).
bool sameDecoded(const cm::UnitStatus& a, const cm::UnitStatus& b) {
  return a.power == b.power && a.display == b.display && a.light == b.light &&
         a.oscillating == b.oscillating && a.celsius == b.celsius && a.fan == b.fan &&
         a.mode == b.mode && a.setpointF == b.setpointF && a.airTempF == b.airTempF &&
         a.setpointRaw == b.setpointRaw && a.batteryV == b.batteryV && a.lvpDeciV == b.lvpDeciV;
}

}  // namespace

Bridge::Bridge(IUnitLink& link) : link_(link), status_{} {
  status_.fan = kUnknown;
  status_.mode = cm::Mode::Unknown;
  status_.setpointF = kUnknown;
  status_.airTempF = kUnknown;
  status_.batteryV = kUnknown;
}

bool Bridge::responding(uint32_t nowMs) const {
  return link_.connected() && heard_ && nowMs - lastFrameMs_ < kRespondingMs;
}

bool Bridge::pendingSetpoint(uint8_t& f) const {
  bool found = false;
  for (size_t k = 0; k < count_; k++) {
    const Intent& q = queue_[(head_ + k) % kMaxPending];
    if (q.op == Op::Setpoint) {
      f = static_cast<uint8_t>(q.value);
      found = true;
    }
  }
  return found;
}

void Bridge::submit(const Intent& i, uint32_t nowMs) {
  if (!checkLink() || !responding(nowMs)) return pushResult(i.tag, Result::Unreachable);

  Intent q = i;
  bool ok;
  switch (i.op) {
    case Op::Power:
    case Op::Display:
    case Op::Oscillate:
    case Op::Light:
    case Op::Celsius: ok = isBool(i.value); break;
    case Op::Mode: ok = i.value >= 0 && i.value <= static_cast<int8_t>(cm::Mode::FanOnly); break;
    case Op::Fan: ok = i.value >= 0 && i.value <= 5; break;
    case Op::ClearPreset: ok = true; break;
    case Op::Setpoint:
    case Op::StepSetpoint: {
      // Resolved now, so back-to-back steps build on each other and refusals are immediate.
      int target = i.value;
      if (i.op == Op::StepSetpoint) {
        uint8_t base = status_.setpointF;
        pendingSetpoint(base);
        ok = (i.value == 1 || i.value == -1) && base != kUnknown;
        target = base + i.value;
      } else {
        ok = true;
      }
      ok = ok && !status_.celsius && target >= cm::kSetpointMinF && target <= cm::kSetpointMaxF;
      q.op = Op::Setpoint;
      q.value = static_cast<int8_t>(target);
      break;
    }
    default: ok = false; break;
  }
  if (!ok || count_ >= kMaxPending) return pushResult(i.tag, Result::Refused);
  queue_[(head_ + count_++) % kMaxPending] = q;
}

bool Bridge::checkLink() {
  if (link_.connected()) return true;
  // Nothing survives a disconnect: old intents must never reach the unit later.
  while (count_ > 0) finish(Result::Unreachable);
  wait_ = Wait::None;
  heard_ = false;
  frames_ = cm::FrameAssembler();
  pollNow_ = true;
  return false;
}

bool Bridge::expand(const Intent& i) {
  cmdCount_ = 0;
  cmdIdx_ = 0;
  writes_ = 0;
  auto add = [this](uint8_t func, uint8_t data) {
    cmds_[cmdCount_][0] = func;
    cmds_[cmdCount_][1] = data;
    cmdCount_++;
  };
  const bool on = status_.power;
  const cm::Mode mode = status_.mode;
  switch (i.op) {
    case Op::Power: add(cm::kFuncPower, i.value ? cm::kPowerOn : cm::kPowerOff); break;
    case Op::Mode: {
      const cm::Mode want = static_cast<cm::Mode>(i.value);
      if (want == cm::Mode::Cool) {
        // The mode reported while off isn't trusted, so always set it after power-on.
        if (!on) add(cm::kFuncPower, cm::kPowerOn);
        else if (cooling(mode)) break;  // already cooling, preset or not
        add(cm::kFuncMode, cm::kModeCool);
      } else if (want == cm::Mode::FanOnly) {
        if (!on) add(cm::kFuncPower, cm::kPowerOn);
        add(cm::kFuncMode, cm::kModeFanOnly);
      } else {
        if (!on) return false;  // a preset implies cooling; ignored while off
        add(cm::kFuncMode, modeData(want));
      }
      break;
    }
    case Op::Fan: add(cm::kFuncFan, i.value == 0 ? cm::kFanAuto : static_cast<uint8_t>(i.value)); break;
    case Op::Setpoint:
      if (status_.celsius) return false;  // switched to °C since submit
      add(cm::kFuncSetpoint, static_cast<uint8_t>(i.value));
      break;
    case Op::Display: add(cm::kFuncDisplay, i.value ? cm::kDisplayOn : cm::kDisplayOff); break;
    case Op::Oscillate: add(cm::kFuncVent, i.value ? cm::kVentOscillating : cm::kVentStatic); break;
    case Op::Light: add(cm::kFuncLight, i.value ? cm::kLightOn : cm::kLightOff); break;
    case Op::Celsius: add(cm::kFuncScale, i.value ? cm::kScaleC : cm::kScaleF); break;
    case Op::ClearPreset:
      if (!on) return false;
      if (isPreset(mode)) add(cm::kFuncMode, cm::kModeCool);
      break;
    default: return false;
  }
  return true;
}

bool Bridge::satisfied(const Intent& i) const {
  const cm::UnitStatus& s = status_;
  const bool v = i.value != 0;
  switch (i.op) {
    case Op::Power: return s.power == v;
    case Op::Mode: {
      const cm::Mode want = static_cast<cm::Mode>(i.value);
      return s.power && (want == cm::Mode::Cool ? cooling(s.mode) : s.mode == want);
    }
    case Op::Fan: return s.fan == i.value;
    case Op::Setpoint: return !s.celsius && s.setpointF == static_cast<uint8_t>(i.value);
    case Op::Display: return s.display == v;
    case Op::Oscillate: return s.oscillating == v;
    case Op::Light: return s.light == v;
    case Op::Celsius: return s.celsius == v;
    case Op::ClearPreset: return s.power && s.mode == cm::Mode::Cool;
    default: return false;
  }
}

bool Bridge::send(uint8_t func, uint8_t data, uint32_t nowMs) {
  uint8_t frame[cm::kCommandLen];
  if (!cm::buildCommand(func, data, frame)) return false;
  // A failed write is handled like a lost reply: the timeout covers it.
  link_.write(frame, sizeof frame);
  wroteAny_ = true;
  lastWriteMs_ = nowMs;
  return true;
}

void Bridge::finish(Result r) {
  pushResult(queue_[head_].tag, r);
  head_ = (head_ + 1) % kMaxPending;
  count_--;
  active_ = false;
  if (wait_ == Wait::Command) wait_ = Wait::None;
}

void Bridge::tick(uint32_t nowMs) {
  if (!checkLink()) return;

  if (wait_ != Wait::None) {
    if (nowMs - lastWriteMs_ < kTimeoutMs) return;
    const bool cmd = wait_ == Wait::Command;
    wait_ = Wait::None;
    if (cmd && writes_ >= kMaxWrites) finish(Result::NotConfirmed);
    // else: a timed-out command is re-sent below; a timed-out poll is simply due again.
  }

  while (count_ > 0 && !active_) {
    if (expand(queue_[head_])) {
      if (cmdCount_ == 0) finish(Result::Ok);  // nothing to do
      else active_ = true;
    } else {
      finish(Result::Refused);
    }
  }

  if (wroteAny_ && nowMs - lastWriteMs_ < kGapMs) return;

  if (active_) {
    if (!send(cmds_[cmdIdx_][0], cmds_[cmdIdx_][1], nowMs)) return finish(Result::Refused);
    writes_++;
    wait_ = Wait::Command;
    return;
  }

  const uint32_t sinceWrite = nowMs - lastWriteMs_;
  const uint32_t sinceFrame = nowMs - lastFrameMs_;
  const uint32_t idle = sinceWrite < sinceFrame ? sinceWrite : sinceFrame;
  if (pollNow_ || idle >= kPollMs) {
    send(cm::kFuncPower, cm::kPowerQuery, nowMs);
    wait_ = Wait::Poll;
    pollNow_ = false;
  }
}

void Bridge::onNotify(const uint8_t* d, size_t n, uint32_t nowMs) {
  frames_.feed(d, n, [&](const uint8_t* f) { onFrame(f, nowMs); });
}

void Bridge::onFrame(const uint8_t* frame, uint32_t nowMs) {
  cm::UnitStatus s;
  if (!cm::decodeStatus(frame, s)) return;
  if (version_ == 0 || !sameDecoded(s, status_)) version_++;
  status_ = s;
  heard_ = true;
  lastFrameMs_ = nowMs;

  // The unit doesn't tag replies: the first frame after a write is taken as its reply.
  if (wait_ == Wait::Poll) {
    wait_ = Wait::None;
  } else if (wait_ == Wait::Command) {
    wait_ = Wait::None;
    if (cmdIdx_ + 1 < cmdCount_) {
      // Intermediate step (power-on before mode): only the final reply decides.
      cmdIdx_++;
      writes_ = 0;
    } else if (satisfied(queue_[head_])) {
      finish(Result::Ok);
    } else if (writes_ >= kMaxWrites) {
      finish(Result::NotConfirmed);
    }
    // else: re-sent once on a later tick; commands are absolute, so that's idempotent.
  }
}

void Bridge::pushResult(uint32_t tag, Result r) {
  // Full: drop the oldest. The callers drain every loop, so this only bites if they stop.
  if (resCount_ == kMaxResults) {
    resHead_ = (resHead_ + 1) % kMaxResults;
    resCount_--;
  }
  results_[(resHead_ + resCount_++) % kMaxResults] = Done{tag, r};
}

bool Bridge::takeResult(uint32_t& tag, Result& r) {
  if (resCount_ == 0) return false;
  tag = results_[resHead_].tag;
  r = results_[resHead_].r;
  resHead_ = (resHead_ + 1) % kMaxResults;
  resCount_--;
  return true;
}

}  // namespace ac
