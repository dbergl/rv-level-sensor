#include "mpu6050.h"

#include <Arduino.h>

namespace {
constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

// Genuine MPU-6050 reports 0x68; common clones/compatible parts report these.
bool knownWhoAmI(uint8_t v) {
  return v == 0x68 || v == 0x70 || v == 0x71 || v == 0x72 || v == 0x73 || v == 0x98;
}

int16_t be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }
}  // namespace

bool Mpu6050::write8(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Mpu6050::readBytes(uint8_t reg, uint8_t* buf, uint8_t len) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(wire_->read());
  return true;
}

bool Mpu6050::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  if (!write8(REG_PWR_MGMT_1, 0x80)) return false;  // device reset
  delay(100);
  if (!write8(REG_PWR_MGMT_1, 0x01)) return false;  // wake, PLL on X gyro
  delay(10);
  if (!readBytes(REG_WHO_AM_I, &whoAmI_, 1) || !knownWhoAmI(whoAmI_)) return false;
  return write8(REG_CONFIG, 0x06) &&        // DLPF ~5 Hz
         write8(REG_SMPLRT_DIV, 9) &&       // 1 kHz / (1 + 9) = 100 Hz
         write8(REG_GYRO_CONFIG, 0x00) &&   // ±250 °/s
         write8(REG_ACCEL_CONFIG, 0x00);    // ±2 g
}

bool Mpu6050::read(level::Vec3& accelG, level::Vec3& gyroDps) {
  if (!wire_) return false;
  uint8_t b[14];
  if (!readBytes(REG_ACCEL_XOUT_H, b, sizeof(b))) return false;
  accelG = {be16(b) / 16384.0f, be16(b + 2) / 16384.0f, be16(b + 4) / 16384.0f};
  gyroDps = {be16(b + 8) / 131.0f, be16(b + 10) / 131.0f, be16(b + 12) / 131.0f};
  // All-zero or all-0xFF data means the bus returned garbage.
  bool allZero = true, allOnes = true;
  for (uint8_t v : b) {
    allZero &= v == 0x00;
    allOnes &= v == 0xFF;
  }
  return !allZero && !allOnes;
}
