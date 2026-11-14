#include "thermostat_codec.h"

namespace rvc {

namespace {

constexpr int kRawAt32F = 8736;  // 0 degC: (273 * 32)
constexpr uint8_t kNA = 0xFF;

// Floor division for a positive divisor (C++ truncates toward zero).
int floorDiv(int n, int d) { return n >= 0 ? n / d : -((-n + d - 1) / d); }

void fill(uint8_t instance, uint8_t out[8]) {
  out[0] = instance;
  for (int i = 1; i < 8; ++i) out[i] = kNA;
}

void putU16(uint16_t v, uint8_t* p) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint8_t presetBits(Preset p) {
  switch (p) {
    case Preset::Sleep: return 0x01;
    case Preset::Eco: return 0x04;
    case Preset::Turbo: return 0x10;
    default: return 0;
  }
}

}  // namespace

uint16_t encodeTempF(int f) {
  // ceil((f - 32) * 160 / 9)
  return static_cast<uint16_t>(kRawAt32F - floorDiv(-(f - 32) * 160, 9));
}

int decodeTempF(uint16_t raw) {
  return 32 + floorDiv((static_cast<int>(raw) - kRawAt32F) * 9 + 80, 160);
}

void packThermoStatus1(const ThermoStatus& s, uint8_t out[8]) {
  fill(s.instance, out);
  out[1] = static_cast<uint8_t>((s.mode & 0x0F) | ((s.fanBits & 3) << 4) | 0xC0);
  out[2] = s.fanRaw;
  putU16(s.coolF < 0 ? 0xFFFF : encodeTempF(s.coolF), &out[5]);
}

void packAmbient(uint8_t instance, int f, uint8_t out[8]) {
  fill(instance, out);
  putU16(f < 0 ? 0xFFFF : encodeTempF(f), &out[1]);
}

void packThermoStatus2(uint8_t instance, bool on, Preset p, uint8_t out[8]) {
  fill(instance, out);
  if (on) out[3] = static_cast<uint8_t>(0xC0 | presetBits(p));
}

void packOptionsStatus(uint8_t instance, bool oscillate, bool display, bool recirculate,
                       uint16_t lvpDeciV, uint8_t out[8]) {
  fill(instance, out);
  out[1] = static_cast<uint8_t>(0xC0 | (recirculate ? 0x10 : 0) | (display ? 0x04 : 0) |
                                (oscillate ? 0x01 : 0));
  putU16(lvpDeciV, &out[2]);
}

void packAcStatus(uint8_t instance, uint8_t fanRaw, uint8_t outputRaw, uint8_t out[8]) {
  fill(instance, out);
  out[1] = 1;    // manual
  out[2] = 200;  // max fan speed 100 %
  out[3] = 200;  // max output level 100 %
  out[4] = fanRaw;
  out[5] = outputRaw;
  out[6] = 10;   // dead band 1.0 degC
}

ThermoCommand1 unpackThermoCommand1(const uint8_t in[8]) {
  return {in[0], static_cast<uint8_t>(in[1] & 0x0F), static_cast<uint8_t>((in[1] >> 4) & 3), in[2],
          static_cast<uint16_t>(in[5] | (in[6] << 8))};
}

ThermoCommand2 unpackThermoCommand2(const uint8_t in[8]) {
  uint8_t code = in[2] & 0x3F;
  return {in[0], code != 0x3F, code};
}

OptionsCommand unpackOptionsCommand(const uint8_t in[8]) {
  return {in[0], static_cast<uint8_t>(in[1] & 3), static_cast<uint8_t>((in[1] >> 2) & 3),
          static_cast<uint8_t>((in[1] >> 4) & 3)};
}

AcCommand unpackAcCommand(const uint8_t in[8]) { return {in[0], in[4], in[5]}; }

}  // namespace rvc
