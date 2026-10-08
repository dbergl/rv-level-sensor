#include "hal/ble_bthome.h"

#include <NimBLEDevice.h>

#include <cstring>
#include <string>

bool BleBthome::begin(const char* name, uint32_t advIntervalMs, uint32_t refreshMs, uint32_t reapplyMs) {
  NimBLEDevice::init(name);
  adv_ = NimBLEDevice::getAdvertising();
  if (adv_ == nullptr) return false;
  // Random start so HA doesn't drop the first packet as a repeat of the last one before a
  // reboot. esp_random() is only truly random once the radio is running, i.e. after init().
  enc_ = bthome::Encoder(static_cast<uint8_t>(esp_random()));
  name_ = name;
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

  uint8_t body[bthome::kMaxBody];
  size_t n = enc_.encode(r, body, sizeof body);
  if (n == 0) return;
  bool changed = n != lastLen_ || std::memcmp(body, last_, n) != 0;
  if (!schedule_.shouldSend(changed || !started_, nowMs)) return;
  // Periodic re-send of unchanged data with a fresh packet ID, so HA processes it again.
  if (!changed && started_) n = enc_.encode(r, body, sizeof body, true);
  std::memcpy(last_, body, n);
  lastLen_ = n;

  NimBLEAdvertisementData d;
  d.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  d.setServiceData(NimBLEUUID(bthome::kServiceUuid), std::string(reinterpret_cast<const char*>(body), n));
  adv_->setAdvertisementData(d);  // NimBLE applies new data while advertising
  NimBLEAdvertisementData sr;       // re-sent too: a host reset clears both
  sr.setName(name_);
  adv_->setScanResponseData(sr);
  if (!started_) started_ = adv_->start();
}

void BleBthome::setConnectable(bool on) {
  if (adv_ == nullptr) return;
  adv_->stop();
  adv_->setAdvertisementType(on ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON);
  if (started_) adv_->start();
}
