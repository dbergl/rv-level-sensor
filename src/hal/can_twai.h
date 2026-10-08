#pragma once
#include <driver/twai.h>

#include "rvc_types.h"

// ESP32 TWAI (CAN) controller at 250 kbit/s with 29-bit identifiers.
class CanTwai : public rvc::ITransmitter {
 public:
  bool begin(int txPin, int rxPin);
  bool send(const rvc::CanFrame& frame) override;  // non-blocking
  bool receive(rvc::CanFrame& out);                // non-blocking; extended data frames only
  // Blocks up to timeoutMs for a frame; the idle CPU sleeps meanwhile. Returns false on timeout.
  bool receive(rvc::CanFrame& out, uint32_t timeoutMs);
  // Handles bus-off: starts recovery, and restarts the controller once recovered.
  // Returns true when a recovery has just completed.
  bool pollRecovered();
  bool busOff() const { return busOff_; }
  uint32_t dropped() const { return dropped_; }

 private:
  bool busOff_ = false;
  uint32_t dropped_ = 0;
};
