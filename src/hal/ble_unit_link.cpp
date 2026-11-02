#include "hal/ble_unit_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "hal/ble_server.h"

namespace {

constexpr uint16_t kService = 0xFFE0;
constexpr uint16_t kNotifyChr = 0xFFE1;
constexpr uint16_t kWriteChr = 0xFFE2;
constexpr uint32_t kScanGraceMs = 5000;  // stop a scan that overran its duration

bool due(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

class ClientCb : public NimBLEClientCallbacks {
 public:
  explicit ClientCb(BleUnitLink& l) : l_(l) {}
  void onDisconnect(NimBLEClient*) override { l_.onDisconnect(); }

 private:
  BleUnitLink& l_;
};

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
 public:
  explicit ScanCb(BleUnitLink& l) : l_(l) {}
  void onResult(NimBLEAdvertisedDevice* d) override {
    if (!d->haveName()) return;
    std::string name = d->getName();
    NimBLEAddress a = d->getAddress();
    l_.onScanResult(name.data(), name.size(), a.getNative(), a.getType(), d->getRSSI());
  }

 private:
  BleUnitLink& l_;
};

// The response only says the write landed; the next status frame says what the unit did.
int onWriteDone(uint16_t, const ble_gatt_error*, ble_gatt_attr*, void*) { return 0; }

}  // namespace

bool BleUnitLink::begin(ac::Bridge& b) {
  bridge_ = &b;
  mu_ = xSemaphoreCreateMutex();
  if (mu_ == nullptr) return false;
  client_ = NimBLEDevice::createClient();
  if (client_ == nullptr) return false;
  client_->setClientCallbacks(new ClientCb(*this), true);
  client_->setConnectTimeout(kConnectTimeoutS);
  scanCb_ = new ScanCb(*this);
  // Same priority as loop(); it only runs while a connect is in progress.
  return xTaskCreatePinnedToCore(workerMain, "ac_link", 6144, this, 1, &worker_, 1) == pdPASS;
}

void BleUnitLink::bindTo(const NimBLEAddress& a) {
  addr_ = a;
  bound_ = true;
  scanWanted_ = false;
  backoff_.reset();
  nextTryMs_ = millis();
}

void BleUnitLink::forget() {
  bound_ = false;
  newBinding_ = false;
  if (state_ == State::Up || state_ == State::Reserving) linkDown(millis(), nullptr);
  // Connecting: poll() drops the link once the worker is done.
}

void BleUnitLink::startBindScan(uint32_t) {
  if (bound_) {
    Serial.printf("AC unit already bound to %s (forget-ac to bind another)\n", addr_.toString().c_str());
    return;
  }
  if (worker_ == nullptr) return;
  scanWanted_ = true;
}

bool BleUnitLink::takeNewBinding(uint8_t addr[6], uint8_t& type) {
  if (!newBinding_ || !bound_) return false;
  newBinding_ = false;
  std::memcpy(addr, addr_.getNative(), 6);
  type = addr_.getType();
  return true;
}

bool BleUnitLink::connected() const { return state_ == State::Up && !dropped_.load(); }

bool BleUnitLink::write(const uint8_t* d, size_t n) {
  if (!connected()) return false;
  // Queued in the host and sent at the next connection event; never blocks the loop.
  return ble_gattc_write_flat(connHandle_, writeHandle_, d, static_cast<uint16_t>(n), onWriteDone, nullptr) == 0;
}

void BleUnitLink::onNotify(const uint8_t* d, size_t n) {
  Lock l(mu_);
  ring_.push(d, n);
}

void BleUnitLink::onScanResult(const char* name, size_t len, const uint8_t addr[6], uint8_t type, int rssi) {
  Lock l(mu_);
  picker_.offer(name, len, addr, type, rssi);
}

void BleUnitLink::workerMain(void* self) {
  auto* link = static_cast<BleUnitLink*>(self);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // NimBLE's own blocking calls use this task's notification too; only a real job counts.
    if (link->job_.load() != Job::Running) continue;
    link->job_ = link->connectAndSubscribe() ? Job::Ok : Job::Failed;
  }
}

bool BleUnitLink::connectAndSubscribe() {
  if (!client_->connect(target_, true)) return false;
  NimBLERemoteService* svc = client_->getService(NimBLEUUID(kService));
  if (svc == nullptr) return false;
  NimBLERemoteCharacteristic* rx = svc->getCharacteristic(NimBLEUUID(kNotifyChr));
  NimBLERemoteCharacteristic* tx = svc->getCharacteristic(NimBLEUUID(kWriteChr));
  if (rx == nullptr || tx == nullptr || !rx->canNotify()) return false;
  auto cb = [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { onNotify(d, n); };
  if (!rx->subscribe(true, cb, true)) return false;
  connHandle_ = client_->getConnId();
  writeHandle_ = tx->getHandle();
  return client_->isConnected();
}

void BleUnitLink::drain(uint32_t nowMs) {
  uint8_t buf[32];
  for (;;) {
    size_t n;
    {
      Lock l(mu_);
      n = ring_.pop(buf, sizeof buf);
    }
    if (n == 0) return;
    bridge_->onNotify(buf, n, nowMs);
  }
}

void BleUnitLink::linkDown(uint32_t nowMs, const char* why) {
  if (client_->isConnected()) client_->disconnect();
  server_.setClientLinks(0);
  state_ = State::Idle;
  nextTryMs_ = nowMs + backoff_.next();
  if (why != nullptr && bound_) {
    Serial.printf("%s, retrying in %lu s\n", why, static_cast<unsigned long>((nextTryMs_ - nowMs) / 1000));
  }
}

void BleUnitLink::poll(uint32_t nowMs) {
  if (worker_ == nullptr) return;  // begin() failed
  drain(nowMs);
  switch (state_) {
    case State::Idle:
      if (bound_ && !scanning_ && due(nowMs, nextTryMs_)) {
        // Count our link before connecting: BleServer::poll (later this loop) stops connectable
        // advertising and frees a server slot if needed, so the connect has a slot.
        server_.setClientLinks(1);
        state_ = State::Reserving;
      }
      break;
    case State::Reserving:
      target_ = addr_;
      dropped_ = false;
      {
        Lock l(mu_);
        ring_.clear();
      }
      job_ = Job::Running;
      xTaskNotifyGive(worker_);
      state_ = State::Connecting;
      break;
    case State::Connecting: {
      Job j = job_.load();
      if (j == Job::Running) break;
      job_ = Job::None;
      if (j == Job::Ok && bound_ && !dropped_.load()) {
        state_ = State::Up;
        backoff_.reset();
        Serial.printf("AC unit %s connected\n", addr_.toString().c_str());
      } else {
        linkDown(nowMs, "AC unit connect failed");
      }
      break;
    }
    case State::Up:
      if (dropped_.load()) linkDown(nowMs, "AC unit disconnected");
      break;
  }
  pollScan(nowMs);
}

void BleUnitLink::pollScan(uint32_t nowMs) {
  if (!scanning_ && !scanWanted_) return;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scanning_) {
    if (scan->isScanning() && nowMs - scanStartMs_ < kScanSeconds * 1000 + kScanGraceMs) return;
    if (scan->isScanning()) scan->stop();
    scanning_ = false;
    ac::BindPicker::Candidate c;
    bool found;
    {
      Lock l(mu_);
      found = picker_.best(c);
    }
    if (bound_) return;  // bound some other way meanwhile
    if (!found) {
      Serial.println("no AC unit (KT...) found; is it powered, and free of other apps? 'pair' to retry");
      return;
    }
    bindTo(NimBLEAddress(c.addr, c.type));
    newBinding_ = true;
    Serial.printf("AC unit bound: %s %s (RSSI %d dBm)\n", c.name, addr_.toString().c_str(), c.rssi);
    return;
  }
  if (!scanWanted_ || bound_ || state_ != State::Idle || job_.load() != Job::None) return;
  scanWanted_ = false;
  {
    Lock l(mu_);
    picker_.reset();
  }
  scan->setAdvertisedDeviceCallbacks(scanCb_, false);
  scan->setActiveScan(true);  // the name may be in the scan response
  scan->setInterval(100);
  scan->setWindow(50);  // half the air time stays with the advert and links
  scan->setMaxResults(0);
  scanning_ = scan->start(kScanSeconds, nullptr, false);
  scanStartMs_ = nowMs;
  Serial.println(scanning_ ? "scanning 10 s for the AC unit (KT...)" : "AC unit scan failed to start; 'pair' to retry");
}
