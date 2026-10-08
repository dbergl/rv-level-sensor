#pragma once
#include <cstdint>

enum class LedMode { Claiming, Level, OutOfLevel, Fault };

// RGB status LED: blue blink = claiming, green = level, green blink = out of level, red = fault.
// Common-anode LED dimmed with LEDC PWM; brightness is the lit fraction in percent.
class StatusLed {
 public:
  StatusLed(int pinR, int pinG, int pinB, uint8_t brightnessPct = 100)
      : r_(pinR), g_(pinG), b_(pinB), brightnessPct_(brightnessPct) {}
  void begin();
  void update(LedMode mode, uint32_t nowMs);

 private:
  void set(bool r, bool g, bool b);
  int r_, g_, b_;
  uint8_t brightnessPct_;
};
