#pragma once
#include <cstddef>
#include <cstdint>

#include "bthome.h"

class NimBLEAdvertising;

// BTHome v2 advertiser. Broadcast-only, except while the update window is open (BleOta). The name goes in the scan response.
class BleBthome {
 public:
  bool begin(const char* name, uint32_t advIntervalMs, uint32_t refreshMs, uint32_t reapplyMs);
  void update(const bthome::Reading& r, uint32_t nowMs);
  // Connectable while the update window is open and no client is connected; the advert data is
  // unchanged, so HA keeps receiving readings. Restarts advertising (a connection stops it).
  void setConnectable(bool on);

 private:
  NimBLEAdvertising* adv_ = nullptr;
  const char* name_ = "";
  bthome::Encoder enc_;
  bthome::AdvertSchedule schedule_{500, 10000};
  bool started_ = false;
  uint8_t last_[bthome::kMaxBody] = {};
  size_t lastLen_ = 0;
};
