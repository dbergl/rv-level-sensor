#include "ac_panel.h"

#include "dgn.h"
#include "thermostat_codec.h"

namespace rvc {
using namespace dgn;

namespace {

constexpr uint8_t kPanelMode0 = 0, kPanelModeCool = 1, kPanelModeFan = 4;
constexpr uint8_t kCmdClear = 0x00, kCmdSleep = 0x01, kCmdEco = 0x04, kCmdTurbo = 0x10;

Preset presetOf(cm::Mode m) {
  switch (m) {
    case cm::Mode::Eco: return Preset::Eco;
    case cm::Mode::Sleep: return Preset::Sleep;
    case cm::Mode::Turbo: return Preset::Turbo;
    default: return Preset::None;
  }
}

}  // namespace

void AcPanel::submit(ac::Op op, int v, uint32_t nowMs) {
  bridge_.submit(ac::Intent{op, static_cast<int8_t>(v), kTagPanel}, nowMs);
}

void AcPanel::onFrame(const CanFrame& f, uint32_t nowMs) {
  Header h = parseId(f.id);
  if (h.sa == ownSa_ || f.len < 8 || f.data[0] != instance_) return;
  switch (h.dgn) {
    case THERMOSTAT_COMMAND_1: onCommand1(f.data, nowMs); break;
    case THERMOSTAT_COMMAND_2: onCommand2(f.data, nowMs); break;
    case FIREFLY_AC_OPTIONS: onOptions(f.data, nowMs); break;
    default: break;
  }
}

void AcPanel::onCommand1(const uint8_t* d, uint32_t nowMs) {
  ThermoCommand1 c = unpackThermoCommand1(d);
  // Heat, dry and anything else the unit can't do is ignored.
  if (c.mode == kPanelMode0) submit(ac::Op::Power, 0, nowMs);
  else if (c.mode == kPanelModeCool) submit(ac::Op::Mode, static_cast<int>(cm::Mode::Cool), nowMs);
  else if (c.mode == kPanelModeFan) submit(ac::Op::Mode, static_cast<int>(cm::Mode::FanOnly), nowMs);

  if (c.fanBits == 0) {
    submit(ac::Op::Fan, 0, nowMs);
  } else if (c.fanRaw > 0 && c.fanRaw <= 200) {
    int n = (c.fanRaw + 20) / 40;  // 0.5 %/bit, 20 % per level
    submit(ac::Op::Fan, n < 1 ? 1 : n > 5 ? 5 : n, nowMs);
  }

  if (c.coolRaw == SETPOINT_UP) submit(ac::Op::StepSetpoint, 1, nowMs);
  else if (c.coolRaw == SETPOINT_DOWN) submit(ac::Op::StepSetpoint, -1, nowMs);
  else if (c.coolRaw < 0xFE00) submit(ac::Op::Setpoint, decodeTempF(c.coolRaw), nowMs);
}

void AcPanel::onCommand2(const uint8_t* d, uint32_t nowMs) {
  ThermoCommand2 c = unpackThermoCommand2(d);
  if (!c.has) return;
  cm::Mode want;
  switch (c.code) {
    case kCmdClear: submit(ac::Op::ClearPreset, 0, nowMs); return;
    case kCmdEco: want = cm::Mode::Eco; break;
    case kCmdSleep: want = cm::Mode::Sleep; break;
    case kCmdTurbo: want = cm::Mode::Turbo; break;
    default: return;
  }
  const cm::UnitStatus& s = bridge_.status();
  // The panel's preset buttons toggle: pressing the active one clears it.
  if (s.power && s.mode == want) submit(ac::Op::ClearPreset, 0, nowMs);
  else submit(ac::Op::Mode, static_cast<int>(want), nowMs);
}

void AcPanel::onOptions(const uint8_t* d, uint32_t nowMs) {
  OptionsCommand c = unpackOptionsCommand(d);
  const cm::UnitStatus& s = bridge_.status();
  if (c.oscillate == 1) submit(ac::Op::Oscillate, !s.oscillating, nowMs);
  else if (c.oscillate == 0) submit(ac::Op::Oscillate, 0, nowMs);
  if (c.display == 1) submit(ac::Op::Display, !s.display, nowMs);
  else if (c.display == 0) submit(ac::Op::Display, 0, nowMs);
}

bool AcPanel::sendFrame(uint32_t dgn, uint8_t sa, const uint8_t d[8]) {
  CanFrame f;
  f.id = makeId(6, dgn, sa);
  f.len = 8;
  for (int i = 0; i < 8; ++i) f.data[i] = d[i];
  return tx_.send(f);
}

void AcPanel::update(bool claimed, uint8_t sa, uint32_t nowMs) {
  ownSa_ = claimed ? sa : ADDR_NULL;
  if (!claimed || !bridge_.responding(nowMs)) {
    sent_ = false;
    return;
  }
  if (sent_ && bridge_.statusVersion() == sentVersion_ && nowMs - sentMs_ < kHeartbeatMs) return;
  sendStatus(sa, nowMs);
}

void AcPanel::sendStatus(uint8_t sa, uint32_t nowMs) {
  const cm::UnitStatus& s = bridge_.status();
  ThermoStatus t{instance_, 0, 3, 0, -1};
  if (s.power) {
    switch (s.mode) {
      case cm::Mode::FanOnly: t.mode = kPanelModeFan; break;
      case cm::Mode::Dehumid: t.mode = 6; break;
      default: t.mode = kPanelModeCool; break;
    }
    if (s.fan >= 1 && s.fan <= 5) {
      t.fanBits = 1;
      t.fanRaw = static_cast<uint8_t>(s.fan * 0x28);
    } else {
      t.fanBits = 0;  // auto, or not known yet
    }
  }
  if (s.setpointF != 0xFF) t.coolF = s.setpointF;
  uint8_t d[8];
  bool ok = true;
  packThermoStatus1(t, d);
  ok &= sendFrame(THERMOSTAT_STATUS_1, sa, d);
  if (s.airTempF != 0xFF) {
    packAmbient(instance_, s.airTempF, d);
    ok &= sendFrame(THERMOSTAT_AMBIENT_STATUS, sa, d);
  }
  packThermoStatus2(instance_, s.power, s.power ? presetOf(s.mode) : Preset::None, d);
  ok &= sendFrame(THERMOSTAT_STATUS_2, sa, d);
  packOptionsStatus(instance_, s.oscillating, s.display, d);
  ok &= sendFrame(FIREFLY_AC_OPTIONS_STATUS, sa, d);
  if (!ok) return;  // retry next update
  sent_ = true;
  sentVersion_ = bridge_.statusVersion();
  sentMs_ = nowMs;
}

}  // namespace rvc
