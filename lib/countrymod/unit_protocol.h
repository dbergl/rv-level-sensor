#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// CountryModPro rooftop AC, Bluetooth wire protocol: command frames, the write allow-list,
// status-frame reassembly and decoding. Pure logic, no I/O.
namespace cm {

constexpr size_t kCommandLen = 9;
constexpr size_t kFrameLen = 21;

constexpr uint8_t kHeader = 0x5A;
constexpr uint8_t kProduct = 0x01;

// func codes
constexpr uint8_t kFuncPower = 0x01;
constexpr uint8_t kFuncMode = 0x02;
constexpr uint8_t kFuncSetpoint = 0x03;
constexpr uint8_t kFuncFan = 0x04;
constexpr uint8_t kFuncDisplay = 0x0A;
constexpr uint8_t kFuncLight = 0x1C;
constexpr uint8_t kFuncScale = 0x20;
constexpr uint8_t kFuncVent = 0x45;

// data values
constexpr uint8_t kPowerQuery = 0x00;
constexpr uint8_t kPowerOff = 0x01;
constexpr uint8_t kPowerOn = 0x02;
constexpr uint8_t kModeCool = 0x01;
constexpr uint8_t kModeFanOnly = 0x02;
constexpr uint8_t kModeEco = 0x04;
constexpr uint8_t kModeSleep = 0x05;
constexpr uint8_t kModeTurbo = 0x06;
constexpr uint8_t kSetpointMinF = 61;
constexpr uint8_t kSetpointMaxF = 86;
constexpr uint8_t kFan1 = 0x01;
constexpr uint8_t kFan5 = 0x05;
constexpr uint8_t kFanAuto = 0x06;
constexpr uint8_t kDisplayOff = 0x01;
constexpr uint8_t kDisplayOn = 0x02;
constexpr uint8_t kVentStatic = 0x00;
constexpr uint8_t kVentOscillating = 0x01;
constexpr uint8_t kLightOn = 0x01;
constexpr uint8_t kLightOff = 0x02;
constexpr uint8_t kScaleC = 0x01;
constexpr uint8_t kScaleF = 0x02;

// Nothing outside this list may ever be written to the unit.
bool isAllowed(uint8_t func, uint8_t data);
// Writes the 9-byte frame; false (out untouched) if not allowed.
bool buildCommand(uint8_t func, uint8_t data, uint8_t out[kCommandLen]);

// Header, trailer and checksum all check out.
bool validFrame(const uint8_t frame[kFrameLen]);

// Reassembles 21-byte status frames from notification fragments. Fixed buffer, no heap.
class FrameAssembler {
 public:
  // onFrame(const uint8_t frame[kFrameLen]) runs for each valid frame, in order.
  template <class F>
  void feed(const uint8_t* d, size_t n, F&& onFrame) {
    for (size_t i = 0; i < n; i++) {
      buf_[len_++] = d[i];
      align();
      if (len_ == kFrameLen) {
        if (validFrame(buf_)) {
          onFrame(static_cast<const uint8_t*>(buf_));
          len_ = 0;
        } else {
          // Bad frame or a lost fragment: slide to the next 5A 5A inside what we hold.
          dropFirst();
          align();
        }
      }
    }
  }
  uint32_t discarded() const { return discarded_; }

 private:
  void dropFirst() {
    memmove(buf_, buf_ + 1, --len_);
    discarded_++;
  }
  // Keeps buf_ starting with a possible header (5A, then 5A once known).
  void align() {
    while (len_ > 0 && (buf_[0] != kHeader || (len_ > 1 && buf_[1] != kHeader))) dropFirst();
  }

  uint8_t buf_[kFrameLen] = {};
  size_t len_ = 0;
  uint32_t discarded_ = 0;
};

enum class Mode : uint8_t { Cool, Eco, Sleep, Turbo, FanOnly, Dehumid, Unknown };

struct UnitStatus {
  bool power;
  bool display;
  bool light;
  bool oscillating;
  bool celsius;
  uint8_t fan;       // 0 auto, 1-5, 0xFF unknown
  Mode mode;
  uint8_t setpointF;  // 0xFF unknown
  uint8_t airTempF;   // 0xFF unknown
  uint8_t setpointRaw;  // as the unit reports it, in its own scale
  uint8_t batteryV;   // 0xFF unknown
  uint16_t lvpDeciV;
  uint8_t raw[kFrameLen];
};

// False if framing or checksum is bad.
bool decodeStatus(const uint8_t frame[kFrameLen], UnitStatus& out);

}  // namespace cm
