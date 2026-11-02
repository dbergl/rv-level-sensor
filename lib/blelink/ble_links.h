#pragma once
#include <cstdint>

// Which BLE links the shared GATT server keeps, and when its advert may be connectable.
// The BTHome broadcast must never stop: NimBLE refuses to advertise at all once the server holds
// MAX_CONNECTIONS links, and refuses connectable advertising with no free host slot (client links
// count too). So one slot always stays free: connectable only while total < max - 1, and a server
// link that takes the total past max - 1 is dropped. Links no service claims within the timeout
// are dropped too, so stray centrals cannot hold slots.
namespace blelink {

constexpr uint16_t kNoLink = 0xFFFF;  // BLE_HS_CONN_HANDLE_NONE

class Links {
 public:
  static constexpr int kSlots = 8;

  Links(uint8_t maxConnections, uint32_t claimTimeoutMs)
      : max_(maxConnections), claimTimeoutMs_(claimTimeoutMs) {}

  // false: over the cap; disconnect it now (it is still counted until onDisconnect).
  bool onConnect(uint16_t h, uint32_t nowMs);
  void onDisconnect(uint16_t h);
  void claim(uint16_t h);
  void setClientLinks(uint8_t n) { clientLinks_ = n; }
  bool connectable(bool wanted) const { return wanted && total() + 1 < max_; }
  // The next server link to disconnect, each returned once: over the cap (newest unclaimed first,
  // then newest), or unclaimed past the timeout. kNoLink if none.
  uint16_t toDrop(uint32_t nowMs);
  int serverLinks() const { return count_; }

 private:
  struct Link {
    uint16_t h;
    uint32_t sinceMs;
    bool claimed;
    bool dropping;  // disconnect requested, waiting for onDisconnect
  };
  int total() const { return count_ + clientLinks_; }
  int find(uint16_t h) const;

  uint8_t max_;
  uint32_t claimTimeoutMs_;
  uint8_t clientLinks_ = 0;
  Link links_[kSlots] = {};  // oldest first
  int count_ = 0;
};

}  // namespace blelink
