#include "status_led.h"

#include <Arduino.h>

#include "led_duty.h"

namespace {
constexpr uint8_t kChR = 0, kChG = 1, kChB = 2;  // LEDC channels
constexpr uint32_t kPwmHz = 5000;
constexpr uint8_t kPwmBits = 8;
}  // namespace

void StatusLed::begin() {
  const int pins[] = {r_, g_, b_};
  for (uint8_t ch = kChR; ch <= kChB; ++ch) {
    ledcSetup(ch, kPwmHz, kPwmBits);
    ledcAttachPin(pins[ch], ch);
  }
  set(false, false, false);
}

void StatusLed::set(bool r, bool g, bool b) {
  ledcWrite(kChR, ledPinDuty(r, brightnessPct_));
  ledcWrite(kChG, ledPinDuty(g, brightnessPct_));
  ledcWrite(kChB, ledPinDuty(b, brightnessPct_));
}

void StatusLed::update(LedMode mode, uint32_t nowMs) {
  bool blinkOn = (nowMs / 250) % 2 == 0;
  switch (mode) {
    case LedMode::Claiming: set(false, false, blinkOn); break;
    case LedMode::Level: set(false, true, false); break;
    case LedMode::OutOfLevel: set(false, blinkOn, false); break;
    case LedMode::Fault: set(true, false, false); break;
  }
}
