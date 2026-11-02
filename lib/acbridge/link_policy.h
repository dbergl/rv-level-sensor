#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// The Bluetooth link to the AC unit, minus the radio: reconnect back-off, choosing the unit to
// bind, and the byte ring that carries notifications to the loop task. Pure logic, no heap.
namespace ac {

// Delay before the next connect attempt: 1 s, doubling to 30 s; reset once a link is up.
class Backoff {
 public:
  static constexpr uint32_t kFirstMs = 1000;
  static constexpr uint32_t kMaxMs = 30000;

  uint32_t next();
  void reset() { nextMs_ = kFirstMs; }

 private:
  uint32_t nextMs_ = kFirstMs;
};

// Picks the unit to bind from a scan: the strongest signal whose name starts "KT".
class BindPicker {
 public:
  static constexpr size_t kNameMax = 24;
  struct Candidate {
    uint8_t addr[6];
    uint8_t type;
    int8_t rssi;
    char name[kNameMax + 1];  // truncated, always terminated
  };

  void reset() { found_ = false; }
  // Ignored unless the name starts "KT".
  void offer(const char* name, size_t len, const uint8_t addr[6], uint8_t type, int rssi);
  bool best(Candidate& out) const;

 private:
  Candidate best_{};
  bool found_ = false;
};

// Fixed byte FIFO. Full: the newest bytes are dropped; the frame assembler resyncs on 5A 5A.
template <size_t N>
class ByteRing {
 public:
  // Bytes stored (may be fewer than n).
  size_t push(const uint8_t* d, size_t n) {
    size_t k = 0;
    for (; k < n && count_ < N; ++k) {
      buf_[(head_ + count_) % N] = d[k];
      ++count_;
    }
    dropped_ += n - k;
    return k;
  }
  size_t pop(uint8_t* out, size_t max) {
    size_t k = 0;
    for (; k < max && count_ > 0; ++k) {
      out[k] = buf_[head_];
      head_ = (head_ + 1) % N;
      --count_;
    }
    return k;
  }
  void clear() { head_ = count_ = 0; }
  size_t size() const { return count_; }
  uint32_t dropped() const { return dropped_; }

 private:
  uint8_t buf_[N] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace ac
