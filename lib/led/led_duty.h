#pragma once
#include <cstdint>

// 8-bit PWM duty for one colour of the status LED. The LED is common-anode (anode to 3.3 V,
// cathodes to the GPIOs through 470 R), so a colour is lit while its pin is LOW.
// 255 holds the pin high (off); the core turns 255 into a true 100 % duty.
inline uint8_t ledPinDuty(bool on, uint8_t brightnessPct) {
  if (!on) return 255;
  if (brightnessPct > 100) brightnessPct = 100;
  return static_cast<uint8_t>(255 - brightnessPct * 255 / 100);
}
