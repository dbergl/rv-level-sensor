#include "multipacket.h"

#include <cstring>

#include "dgn.h"
#include "frame.h"

namespace rvc {

bool MultiPacketTx::start(uint32_t dgn, const uint8_t* data, uint16_t len) {
  if (busy_ || len == 0 || len > kMaxLen) return false;
  std::memcpy(buf_, data, len);
  len_ = len;
  packets_ = static_cast<uint8_t>((len + 6) / 7);
  next_ = 0;
  dgn_ = dgn;
  busy_ = true;
  return true;
}

void MultiPacketTx::poll(uint32_t nowMs, uint8_t sa, ITransmitter& tx) {
  if (!busy_) return;
  if (next_ > 0 && nowMs - lastMs_ < kGapMs) return;

  CanFrame f;
  if (next_ == 0) {
    f.id = makeId(prio::NORMAL, dgn::INITIAL_PACKET | ADDR_GLOBAL, sa);
    f.data[0] = 0x20;
    f.data[1] = static_cast<uint8_t>(len_ & 0xFF);
    f.data[2] = static_cast<uint8_t>(len_ >> 8);
    f.data[3] = packets_;
    f.data[4] = 0xFF;
    f.data[5] = static_cast<uint8_t>(dgn_ & 0xFF);
    f.data[6] = static_cast<uint8_t>((dgn_ >> 8) & 0xFF);
    f.data[7] = static_cast<uint8_t>((dgn_ >> 16) & 0xFF);
  } else {
    f.id = makeId(prio::NORMAL, dgn::DATA_PACKET | ADDR_GLOBAL, sa);
    f.data[0] = next_;
    uint16_t offset = static_cast<uint16_t>((next_ - 1) * 7);
    for (uint16_t i = 0; i < 7 && offset + i < len_; ++i) f.data[1 + i] = buf_[offset + i];
  }
  if (!tx.send(f)) return;
  lastMs_ = nowMs;
  if (++next_ > packets_) busy_ = false;
}

}  // namespace rvc
