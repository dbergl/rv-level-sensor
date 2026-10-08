#include "codec.h"

#include <cmath>

namespace rvc {

uint16_t encodeAngle(float deg) {
  long v = std::lround((static_cast<double>(deg) + 200.0) * 128.0);
  if (v < 0) v = 0;
  if (v > 64000) v = 64000;
  return static_cast<uint16_t>(v);
}

float decodeAngle(uint16_t raw) { return static_cast<float>(raw) / 128.0f - 200.0f; }

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void packSensorStatus(const SensorStatusMsg& m, uint8_t out[8]) {
  if (m.valid) {
    out[0] = static_cast<uint8_t>((m.bits.driverLow ? 0x01 : 0) | (m.bits.frontLow ? 0x04 : 0) |
                                  (m.bits.passengerLow ? 0x10 : 0) | (m.bits.rearLow ? 0x40 : 0));
    put16(out + 1, encodeAngle(m.pitchDeg));
    put16(out + 3, encodeAngle(m.rollDeg));
  } else {
    out[0] = 0xFF;
    put16(out + 1, U16_ERR);
    put16(out + 3, U16_ERR);
  }
  put16(out + 5, U16_NA);  // secondary pitch not supported
  out[7] = m.instance;
}

void packAxisConfig(const AxisConfigMsg& m, uint8_t out[8]) {
  out[0] = m.instance;
  put16(out + 1, m.outRaw);
  put16(out + 3, m.withinRaw);
  out[5] = static_cast<uint8_t>(0xFC | (m.setZero & 0x3));
  out[6] = 0xFF;
  out[7] = 0xFF;
}

AxisConfigMsg unpackAxisConfig(const uint8_t in[8]) {
  AxisConfigMsg m;
  m.instance = in[0];
  m.outRaw = get16(in + 1);
  m.withinRaw = get16(in + 3);
  m.setZero = in[5] & 0x3;
  return m;
}

void packDmRv(const DmRvMsg& m, uint8_t out[8]) {
  out[0] = static_cast<uint8_t>(0x05 | (m.fault ? 0x10 : 0));  // ON, active, yellow lamp if fault
  out[1] = m.dsa;
  if (m.fault) {
    out[2] = static_cast<uint8_t>((m.spn >> 11) & 0xFF);
    out[3] = static_cast<uint8_t>((m.spn >> 3) & 0xFF);
    out[4] = static_cast<uint8_t>(((m.spn & 0x7) << 5) | (m.fmi & 0x1F));
    out[5] = 0xFF;  // occurrence count not available (7Fh) + reserved bit
  } else {
    out[2] = out[3] = out[4] = out[5] = 0xFF;
  }
  out[6] = 0xFF;
  out[7] = 0xFF;
}

void packAck(uint8_t code, uint8_t instance, uint8_t ackedSa, uint32_t ackedDgn, uint8_t out[8]) {
  out[0] = code;
  out[1] = instance;
  out[2] = 0xFF;
  out[3] = 0xFF;
  out[4] = ackedSa;
  out[5] = static_cast<uint8_t>(ackedDgn & 0xFF);
  out[6] = static_cast<uint8_t>((ackedDgn >> 8) & 0xFF);
  out[7] = static_cast<uint8_t>((ackedDgn >> 16) & 0xFF);
}

RequestMsg unpackRequest(const uint8_t in[8]) {
  RequestMsg r;
  r.dgn = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
          (static_cast<uint32_t>(in[2] & 0x01) << 16);
  r.instance = in[3];
  return r;
}

void packRequest(uint32_t dgn, uint8_t out[8]) {
  out[0] = static_cast<uint8_t>(dgn & 0xFF);
  out[1] = static_cast<uint8_t>((dgn >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((dgn >> 16) & 0xFF);
  for (int i = 3; i < 8; ++i) out[i] = 0xFF;
}

uint64_t makeName(uint32_t serial21, uint16_t manufacturer11) {
  return (1ULL << 63) | (static_cast<uint64_t>(manufacturer11 & 0x7FF) << 21) | (serial21 & 0x1FFFFF);
}

void packName(uint64_t name, uint8_t out[8]) {
  for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(name >> (8 * i));
}

uint64_t unpackName(const uint8_t in[8]) {
  uint64_t n = 0;
  for (int i = 0; i < 8; ++i) n |= static_cast<uint64_t>(in[i]) << (8 * i);
  return n;
}

}  // namespace rvc
