#pragma once
#include <cstddef>
#include <cstdint>

#include "unit_protocol.h"

// Serializes AC commands from the panel and HA onto the CountryModPro unit. Every status frame
// the unit sends is the truth; an intent succeeds only if a reply shows it done. Fixed storage,
// no heap.
namespace ac {

constexpr uint32_t kGapMs = 400;
constexpr uint32_t kTimeoutMs = 3000;
constexpr uint32_t kPollMs = 3000;
constexpr uint32_t kRespondingMs = 10000;
// An HA intent (tag != 0) not started by then fails NotConfirmed, unwritten: HA gives up on a
// command, so it must not change the unit later.
constexpr uint32_t kStartDeadlineMs = 10000;
constexpr size_t kMaxPending = 8;
constexpr size_t kMaxResults = 16;

class IUnitLink {
 public:
  virtual ~IUnitLink() = default;
  virtual bool connected() const = 0;
  virtual bool write(const uint8_t* d, size_t n) = 0;
};

enum class Op : uint8_t {
  Power, Mode, Fan, Setpoint, StepSetpoint, Display, Oscillate, Light, Celsius,
  ClearPreset,  // back to plain cool if a preset is active; value ignored
};

// Mode values are cm::Mode (Cool, Eco, Sleep, Turbo, FanOnly); Fan 0 = auto, 1-5; Setpoint in
// °F; StepSetpoint ±1; bools 0/1. tag is echoed in the result.
struct Intent {
  Op op;
  int8_t value;
  uint32_t tag;
};

enum class Result : uint8_t { Ok = 0, Refused = 1, Unreachable = 2, NotConfirmed = 3, BadAuth = 4 };

class Bridge {
 public:
  explicit Bridge(IUnitLink& link);

  // Always yields exactly one result via takeResult, possibly immediately.
  void submit(const Intent& i, uint32_t nowMs);
  void onNotify(const uint8_t* d, size_t n, uint32_t nowMs);
  void tick(uint32_t nowMs);

  bool responding(uint32_t nowMs) const;
  const cm::UnitStatus& status() const { return status_; }
  // Bumps only when a decoded field changes.
  uint32_t statusVersion() const { return version_; }
  bool takeResult(uint32_t& tag, Result& r);

 private:
  static constexpr size_t kMaxCmds = 2;
  static constexpr uint8_t kMaxWrites = 2;  // a command and one retry
  enum class Wait : uint8_t { None, Poll, Command };

  void onFrame(const uint8_t* frame, uint32_t nowMs);
  bool checkLink();
  bool expand(const Intent& i);
  bool satisfied(const Intent& i) const;
  bool send(uint8_t func, uint8_t data, uint32_t nowMs);
  void finish(Result r);
  void pushResult(uint32_t tag, Result r);
  bool pendingSetpoint(uint8_t& f) const;

  IUnitLink& link_;
  cm::FrameAssembler frames_;
  cm::UnitStatus status_;
  uint32_t version_ = 0;
  bool heard_ = false;  // a frame since the link came up
  uint32_t lastFrameMs_ = 0;

  // FIFO of intents; the front one is executing once active_ is set. Setpoint/StepSetpoint are
  // stored as Setpoint with the absolute target resolved at submit.
  Intent queue_[kMaxPending];
  uint32_t queuedMs_[kMaxPending];  // submit time, per queue_ slot
  size_t head_ = 0;
  size_t count_ = 0;
  bool active_ = false;
  uint8_t cmds_[kMaxCmds][2];
  uint8_t cmdCount_ = 0;
  uint8_t cmdIdx_ = 0;
  uint8_t writes_ = 0;  // for the current command

  Wait wait_ = Wait::None;
  bool wroteAny_ = false;
  uint32_t lastWriteMs_ = 0;
  bool pollNow_ = true;

  struct Done {
    uint32_t tag;
    Result r;
  };
  Done results_[kMaxResults];
  size_t resHead_ = 0;
  size_t resCount_ = 0;
};

}  // namespace ac
