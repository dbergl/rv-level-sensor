#include "hal/ble_bthome.h"

#include <NimBLEDevice.h>

#include <cstring>
#include <string>

bool BleBthome::begin(const char* name, uint32_t advIntervalMs, uint32_t refreshMs, uint32_t reapplyMs,
                      const char* serviceUuid) {
  NimBLEDevice::init(name);
  adv_ = NimBLEDevice::getAdvertising();
  if (adv_ == nullptr) return false;
  // Random start so HA doesn't drop the first packet as a repeat of the last one before a
  // reboot. esp_random() is only truly random once the radio is running, i.e. after init().
  enc_ = bthome::Encoder(static_cast<uint8_t>(esp_random()));
  name_ = name;
  serviceUuid_ = serviceUuid;
  schedule_ = bthome::AdvertSchedule(refreshMs, reapplyMs);
  uint16_t units = static_cast<uint16_t>(advIntervalMs * 1000 / 625);
  adv_->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  adv_->setMinInterval(units);
  adv_->setMaxInterval(units + 16);  // +10 ms
  adv_->setScanResponse(true);  // scan response (the name) is set in update() with the data
  return true;
}

void BleBthome::update(const bthome::Reading& r, uint32_t nowMs) {
  if (adv_ == nullptr) return;
  if (!schedule_.refreshDue(nowMs)) return;
  heal();

  uint8_t body[bthome::kMaxBody];
  size_t n = enc_.encode(r, body, sizeof body);
  if (n == 0) return;
  bool changed = n != lastLen_ || std::memcmp(body, last_, n) != 0;
  if (!schedule_.shouldSend(changed || !started_, nowMs)) return;
  // Periodic re-send of unchanged data with a fresh packet ID, so HA processes it again.
  if (!changed && started_) n = enc_.encode(r, body, sizeof body, true);
  std::memcpy(last_, body, n);
  lastLen_ = n;
  apply(body, n);
}

void BleBthome::update(uint32_t nowMs) {
  if (adv_ == nullptr) return;
  if (!schedule_.refreshDue(nowMs)) return;
  heal();
  if (!schedule_.shouldSend(!started_, nowMs)) return;  // nothing changes; re-applied every reapplyMs
  apply(nullptr, 0);
}

void BleBthome::apply(const uint8_t* body, size_t n) {
  NimBLEAdvertisementData d;
  d.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  if (n > 0) {
    d.setServiceData(NimBLEUUID(bthome::kServiceUuid), std::string(reinterpret_cast<const char*>(body), n));
  } else {
    d.setName(name_);  // for passive scanners, which never see the scan response
  }
  adv_->setAdvertisementData(d);  // NimBLE applies new data while advertising
  NimBLEAdvertisementData sr;       // re-sent too: a host reset clears both
  sr.setName(name_);
  if (serviceUuid_ != nullptr) sr.setCompleteServices(NimBLEUUID(serviceUuid_));
  adv_->setScanResponseData(sr);
  if (!started_) started_ = start();
}

// The broadcast must never stop: NimBLE refuses a connectable start with no free link slot, so
// fall back to non-connectable rather than not advertising.
bool BleBthome::start() {
  if (adv_->start()) return true;
  if (!connectable_) return false;
  adv_->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  connectable_ = false;
  return adv_->start();
}

// Restart advertising if something stopped it (a connection, a failed start).
void BleBthome::heal() {
  if (started_ && !adv_->isAdvertising()) start();
}

void BleBthome::setConnectable(bool on) {
  if (adv_ == nullptr) return;
  adv_->stop();
  adv_->setAdvertisementType(on ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON);
  connectable_ = on;
  if (started_) start();
}
