#include "hal/ble_ota.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>

#include "hal/ble_bthome.h"
#include "hal/ota_flash.h"
#include "ota_pubkey.h"

namespace {

constexpr const char* kServiceUuid = "af480001-29e2-47f0-a341-4f1a49271bf4";
constexpr const char* kInfoUuid = "af480002-29e2-47f0-a341-4f1a49271bf4";
constexpr const char* kControlUuid = "af480003-29e2-47f0-a341-4f1a49271bf4";
constexpr const char* kDataUuid = "af480004-29e2-47f0-a341-4f1a49271bf4";
constexpr size_t kVersionMax = 40;

class ServerCb : public NimBLEServerCallbacks {
 public:
  explicit ServerCb(BleOta& o) : o_(o) {}
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override { o_.onConnect(desc->conn_handle); }
  void onDisconnect(NimBLEServer*) override { o_.onDisconnect(); }

 private:
  BleOta& o_;
};

class ControlCb : public NimBLECharacteristicCallbacks {
 public:
  explicit ControlCb(BleOta& o) : o_(o) {}
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v = c->getValue();
    o_.onControlWrite(v.data(), v.length());
  }

 private:
  BleOta& o_;
};

class DataCb : public NimBLECharacteristicCallbacks {
 public:
  explicit DataCb(BleOta& o) : o_(o) {}
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v = c->getValue();
    o_.onDataWrite(v.data(), v.length());
  }

 private:
  BleOta& o_;
};

class InfoCb : public NimBLECharacteristicCallbacks {
 public:
  explicit InfoCb(BleOta& o) : o_(o) {}
  void onRead(NimBLECharacteristic*) override { o_.onInfoRead(); }

 private:
  BleOta& o_;
};

}  // namespace

BleOta::BleOta(OtaFlash& flash, BleBthome& adv, const char* version)
    : adv_(adv), version_(version), session_(flash, kOtaPublicKey) {}

bool BleOta::begin() {
  mu_ = xSemaphoreCreateMutex();
  if (mu_ == nullptr) return false;
  NimBLEDevice::setMTU(517);
  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) return false;
  server_->setCallbacks(new ServerCb(*this));
  server_->advertiseOnDisconnect(false);  // poll() owns advertising
  NimBLEService* svc = server_->createService(kServiceUuid);
  info_ = svc->createCharacteristic(kInfoUuid, NIMBLE_PROPERTY::READ);
  info_->setCallbacks(new InfoCb(*this));
  control_ = svc->createCharacteristic(kControlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  control_->setCallbacks(new ControlCb(*this));
  NimBLECharacteristic* data = svc->createCharacteristic(kDataUuid, NIMBLE_PROPERTY::WRITE_NR);
  data->setCallbacks(new DataCb(*this));
  return svc->start();
}

void BleOta::openWindow(uint32_t nowMs) {
  Lock l(mu_);
  session_.openWindow(nowMs);
}

void BleOta::poll(uint32_t nowMs) {
  bool open;
  {
    Lock l(mu_);
    session_.tick(nowMs);
    open = session_.windowOpen();
  }
  bool connected = connected_.load();
  if (!open && connected && !dropRequested_) {
    server_->disconnect(connHandle_.load());
    dropRequested_ = true;
  }
  bool want = open && !connected;
  if (want != connectable_ || linkChanged_.exchange(false)) {
    adv_.setConnectable(want);
    connectable_ = want;
  }
}

bool BleOta::rebootDue(uint32_t nowMs) {
  Lock l(mu_);
  return session_.rebootDue(nowMs);
}

bool BleOta::windowOpen() {
  Lock l(mu_);
  return session_.windowOpen();
}

void BleOta::onConnect(uint16_t connHandle) {
  connHandle_ = connHandle;
  connected_ = true;
  dropRequested_ = false;
  linkChanged_ = true;  // the connection stopped advertising; poll() restarts it non-connectable
}

void BleOta::onDisconnect() {
  {
    Lock l(mu_);
    session_.onDisconnect();
  }
  connected_ = false;
  linkChanged_ = true;
}

void BleOta::notify(const ota::Notice& n) {
  uint8_t b[5];
  ota::packNotice(n, b);
  control_->setValue(b, sizeof b);
  control_->notify();
}

void BleOta::onControlWrite(const uint8_t* d, size_t len) {
  ota::Notice n;
  {
    Lock l(mu_);
    n = session_.onControl(d, len, millis());
  }
  notify(n);
}

void BleOta::onDataWrite(const uint8_t* d, size_t len) {
  ota::Notice n;
  bool send;
  {
    Lock l(mu_);
    send = session_.onData(d, len, millis(), n);
  }
  if (send) notify(n);
}

void BleOta::onInfoRead() {
  uint8_t b[4 + kVersionMax];
  uint16_t secs;
  {
    Lock l(mu_);
    secs = session_.secondsLeft(millis());
  }
  b[0] = OtaFlash::runningOnTrial() ? 1 : 0;
  b[1] = OtaFlash::runningSlot();
  b[2] = static_cast<uint8_t>(secs);
  b[3] = static_cast<uint8_t>(secs >> 8);
  size_t n = std::strlen(version_);
  if (n > kVersionMax) n = kVersionMax;
  std::memcpy(b + 4, version_, n);
  info_->setValue(b, 4 + n);
}
