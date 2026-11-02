#include "unit_protocol.h"

namespace cm {

namespace {

constexpr uint8_t kUnknown = 0xFF;

uint8_t sum(const uint8_t* p, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++) s = static_cast<uint8_t>(s + p[i]);
  return s;
}

// Whole degrees; the unit reports whole degrees in either scale. Unknown if it doesn't fit a
// byte: garbage °C must not wrap into a believable °F.
uint8_t toF(uint8_t v, bool celsius) {
  if (!celsius) return v;
  const int f = (v * 18 + 325) / 10;  // round(v * 9 / 5 + 32)
  return f <= 254 ? static_cast<uint8_t>(f) : kUnknown;
}

uint8_t inRange(uint8_t f, uint8_t lo, uint8_t hi) { return (f >= lo && f <= hi) ? f : kUnknown; }

}  // namespace

bool isAllowed(uint8_t func, uint8_t data) {
  switch (func) {
    case kFuncPower: return data <= kPowerOn;
    case kFuncMode: return data == kModeCool || data == kModeFanOnly || (data >= kModeEco && data <= kModeTurbo);
    case kFuncSetpoint: return data >= kSetpointMinF && data <= kSetpointMaxF;
    case kFuncFan: return data >= kFan1 && data <= kFanAuto;
    case kFuncDisplay: return data == kDisplayOff || data == kDisplayOn;
    case kFuncVent: return data == kVentStatic || data == kVentOscillating;
    case kFuncLight: return data == kLightOn || data == kLightOff;
    case kFuncScale: return data == kScaleC || data == kScaleF;
    default: return false;
  }
}

bool buildCommand(uint8_t func, uint8_t data, uint8_t out[kCommandLen]) {
  if (!isAllowed(func, data)) return false;
  // len = data bytes + 5; every command here has one data byte.
  const uint8_t f[kCommandLen] = {kHeader, kHeader, 6, kProduct, func, data, 0, 0x0D, 0x0A};
  memcpy(out, f, kCommandLen);
  out[6] = sum(out, 6);
  return true;
}

bool validFrame(const uint8_t f[kFrameLen]) {
  return f[0] == kHeader && f[1] == kHeader && f[19] == 0x0D && f[20] == 0x0A &&
         f[18] == sum(f, 18);
}

bool decodeStatus(const uint8_t f[kFrameLen], UnitStatus& out) {
  if (!validFrame(f)) return false;
  memcpy(out.raw, f, kFrameLen);
  out.power = f[2] & 0x80;
  out.display = f[2] & 0x01;
  out.fan = (f[2] >> 1) & 7;
  if (out.fan > 5) out.fan = kUnknown;
  out.light = f[3] & 0x02;
  out.oscillating = f[3] & 0x08;
  out.celsius = f[16] == 0;

  // 0x40 is set while the vendor app is connected; it says nothing about the mode.
  switch (f[3] & 0xB0) {
    case 0x10: out.mode = Mode::Sleep; break;
    case 0x20: out.mode = Mode::Eco; break;
    case 0x80: out.mode = Mode::Turbo; break;
    case 0x00: {
      const uint8_t base = f[2] & 0x30;
      out.mode = base == 0x30 ? Mode::FanOnly : base == 0x20 ? Mode::Dehumid : Mode::Cool;
      break;
    }
    default: out.mode = Mode::Unknown; break;
  }

  out.airTempF = inRange(toF(f[4], out.celsius), 30, 130);
  out.setpointRaw = f[17];
  out.setpointF = inRange(toF(f[17], out.celsius), 50, 99);
  out.batteryV = inRange(f[8], 20, 70);
  out.lvpDeciV = static_cast<uint16_t>(256 + f[9]);
  return true;
}

}  // namespace cm
