#pragma once
#include <cstddef>
#include <cstdint>

#include "ota_sha256.h"

// Firmware transfer over the Bluetooth update service
// (docs/protocol.md). Pure: time comes in as nowMs and
// flash goes through FlashSink, so the whole protocol is host tested.
namespace ota {

enum class Code : uint8_t {
  Ok = 0x00,
  Progress = 0x01,
  Resend = 0x02,
  Done = 0x03,
  ErrWindowClosed = 0x10,
  ErrBadSize = 0x11,
  ErrNotStarted = 0x12,
  ErrIncomplete = 0x13,
  ErrHash = 0x14,
  ErrInvalidImage = 0x15,
  ErrFlash = 0x16,
  ErrBadCommand = 0x17,
  ErrSignature = 0x18,
};

struct Notice {
  Code code;
  uint32_t value;
};

void packNotice(const Notice& n, uint8_t out[5]);  // code, then value little endian

namespace op {
constexpr uint8_t BEGIN = 0x01;   // size u32, SHA-256[32], Ed25519 signature[64]
constexpr uint8_t FINISH = 0x02;
constexpr uint8_t ABORT = 0x03;
constexpr uint8_t CLOSE = 0x04;
}  // namespace op

constexpr uint32_t kWindowMs = 300000;
constexpr uint32_t kProgressEvery = 4096;
constexpr uint32_t kRebootDelayMs = 500;
constexpr size_t kBeginLen = 101;        // opcode, size, SHA-256, signature
constexpr size_t kUnsignedBeginLen = 37;  // without the signature: refused with ErrSignature 0

// Where the image goes. Each call returns 0 or an esp_err_t. finish() validates the image and makes
// it the boot image; it releases the transfer whether or not it succeeds.
class FlashSink {
 public:
  virtual ~FlashSink() = default;
  virtual uint32_t capacity() const = 0;
  virtual int32_t begin(uint32_t size) = 0;
  virtual int32_t write(const uint8_t* data, size_t len) = 0;
  virtual int32_t finish() = 0;
  virtual void abort() = 0;
};

class Session {
 public:
  // publicKey: the 32-byte Ed25519 key images must be signed with; must outlive the session.
  Session(FlashSink& sink, const uint8_t* publicKey) : sink_(sink), publicKey_(publicKey) {}

  void openWindow(uint32_t nowMs);  // opens, or moves the deadline to now + 5 min
  bool windowOpen() const { return open_; }
  void tick(uint32_t nowMs);        // closes the window at its deadline, aborting any transfer
  uint16_t secondsLeft(uint32_t nowMs) const;

  Notice onControl(const uint8_t* d, size_t len, uint32_t nowMs);
  // True, with `out` set, when this data write needs a notification.
  bool onData(const uint8_t* d, size_t len, uint32_t nowMs, Notice& out);
  void onDisconnect();  // aborts an unfinished transfer; the window stays open

  // Signed: the caller's nowMs may be read on another task a moment before DONE was recorded.
  bool rebootDue(uint32_t nowMs) const {
    return done_ && static_cast<int32_t>(nowMs - doneMs_) >= static_cast<int32_t>(kRebootDelayMs);
  }
  bool transferring() const { return active_; }

 private:
  void extend(uint32_t nowMs) { deadlineMs_ = nowMs + kWindowMs; }
  void abortTransfer();
  Notice begin(const uint8_t* d, size_t len);
  Notice finish(uint32_t nowMs);

  FlashSink& sink_;
  const uint8_t* publicKey_;
  bool open_ = false;
  uint32_t deadlineMs_ = 0;
  bool active_ = false;
  uint32_t size_ = 0;
  uint32_t received_ = 0;
  uint8_t expected_[32] = {};
  Sha256 hash_;
  bool resendSent_ = false;  // one RESEND per gap
  bool done_ = false;
  uint32_t doneMs_ = 0;
};

}  // namespace ota
