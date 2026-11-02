#pragma once
#include <cstddef>
#include <cstdint>

#include "bthome.h"

class NimBLEAdvertising;

// BTHome v2 advertiser. Broadcast-only, except while a GATT service wants connections
// (BleServer). The name goes in the scan response, followed by serviceUuid when one is given.
class BleBthome {
 public:
  bool begin(const char* name, uint32_t advIntervalMs, uint32_t refreshMs, uint32_t reapplyMs,
             const char* serviceUuid);
  void update(const bthome::Reading& r, uint32_t nowMs);
  // Without leveling: an advert with the name and no readings, so the GATT service can be found.
  void update(uint32_t nowMs);
  // Connectable or not; the advert data is unchanged, so HA keeps receiving readings. Restarts
  // advertising (a connection stops it).
  void setConnectable(bool on);

 private:
  void apply(const uint8_t* body, size_t n);  // n == 0: no readings

  NimBLEAdvertising* adv_ = nullptr;
  const char* name_ = "";
  const char* serviceUuid_ = nullptr;
  bthome::Encoder enc_;
  bthome::AdvertSchedule schedule_{500, 10000};
  bool started_ = false;
  uint8_t last_[bthome::kMaxBody] = {};
  size_t lastLen_ = 0;
};
