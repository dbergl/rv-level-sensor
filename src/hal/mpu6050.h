#pragma once
#include <Wire.h>

#include "attitude.h"

// Minimal MPU-6050 register driver: ±2 g, ±250 °/s, DLPF ~5 Hz, 100 Hz sample rate.
class Mpu6050 {
 public:
  bool begin(TwoWire& wire, uint8_t address = 0x68);
  bool read(level::Vec3& accelG, level::Vec3& gyroDps);
  uint8_t whoAmI() const { return whoAmI_; }

 private:
  bool write8(uint8_t reg, uint8_t value);
  bool readBytes(uint8_t reg, uint8_t* buf, uint8_t len);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0x68;
  uint8_t whoAmI_ = 0;
};
