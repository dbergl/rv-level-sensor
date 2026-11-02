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

struct AddrText {
  explicit AddrText(const NimBLEAddress& a) { ac::formatAddress(a.getNative(), s); }
  char s[ac::kAddrTextLen];
};

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

int writeDone(uint16_t, const ble_gatt_error* error, ble_gatt_attr*, void* arg) {
  static_cast<BleUnitLink*>(arg)->onWriteDone(error != nullptr ? error->status : 0);
  return 0;
}

}  // namespace

// Not the uint8_t[6] constructor: that one reverses the bytes (bluedroid order).
NimBLEAddress BleUnitLink::fromNative(const uint8_t addr[6], uint8_t type) {
  ble_addr_t a;
  a.type = type;
  std::memcpy(a.val, addr, 6);
  return NimBLEAddress(a);
}

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
  candidate_ = false;
  scanWanted_ = false;
  backoff_.reset();
  nextTryMs_ = millis();
}

void BleUnitLink::forget() {
  bound_ = false;
  candidate_ = false;
  newBinding_ = false;
  if (state_ == State::Up || state_ == State::Reserving) linkDown(millis(), state_ == State::Up, nullptr);
  // Connecting: poll() drops the link once the worker is done.
}

void BleUnitLink::startBindScan(uint32_t) {
  if (bound_) {
    Serial.printf("AC unit already bound to %s (forget-ac to bind another)\n", AddrText(addr_).s);
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
  if (!connected() || writeBusy_.exchange(true)) return false;  // one ATT request at a time
  // Queued in the host and sent at the next connection event; never blocks the loop.
  if (ble_gattc_write_flat(connHandle_, writeHandle_, d, static_cast<uint16_t>(n), writeDone, this) == 0) {
    return true;
  }
  writeBusy_ = false;
  return false;
}

void BleUnitLink::onWriteDone(uint16_t status) {
  if (status != 0) writeError_ = status;
  writeBusy_ = false;
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

// established: connect() succeeded, so NimBLE reports the disconnect (onDisconnect) and the next
// attempt waits for it rather than have a late event spoil that attempt.
void BleUnitLink::linkDown(uint32_t nowMs, bool established, const char* why) {
  awaitDrop_ = (established || client_->isConnected()) && !dropped_.load();
  awaitDropUntilMs_ = nowMs + kDisconnectWaitMs;
  if (client_->isConnected()) client_->disconnect();
  server_.setClientLinks(0);
  state_ = State::Idle;
  nextTryMs_ = nowMs + backoff_.next();
  if (why != nullptr && bound_) {
    Serial.printf("%s, retrying in %lu s\n", why, static_cast<unsigned long>((nextTryMs_ - nowMs) / 1000));
  }
}

void BleUnitLink::reject(uint32_t nowMs, bool established, const char* reason) {
  Serial.printf("AC candidate %s %s rejected: %s\n", candName_, AddrText(addr_).s, reason);
  candidate_ = false;  // never tried again; 'pair' scans afresh
  linkDown(nowMs, established, nullptr);
}

bool BleUnitLink::readyToConnect(uint32_t nowMs) {
  if (!wanted() || scanning_ || !due(nowMs, nextTryMs_) || client_->isConnected()) return false;
  if (awaitDrop_ && !dropped_.load() && !due(nowMs, awaitDropUntilMs_)) return false;
  awaitDrop_ = false;
  return true;
}

void BleUnitLink::poll(uint32_t nowMs) {
  if (worker_ == nullptr) return;  // begin() failed
  drain(nowMs);
  if (uint16_t err = writeError_.exchange(0)) Serial.printf("AC unit write failed (status 0x%X)\n", err);
  switch (state_) {
    case State::Idle:
      if (readyToConnect(nowMs)) {
        // Count our link before connecting: BleServer::poll (later this loop) stops connectable
        // advertising and frees a server slot if needed, so the connect has a slot.
        server_.setClientLinks(1);
        state_ = State::Reserving;
      }
      break;
    case State::Reserving:
      target_ = addr_;
      dropped_ = false;
      writeBusy_ = false;
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
      bool ok = j == Job::Ok;
      if (ok && wanted() && !dropped_.load()) {
        state_ = State::Up;
        if (candidate_) {
          verifyUntilMs_ = nowMs + kVerifyMs;
        } else {
          backoff_.reset();
          Serial.printf("AC unit %s connected\n", AddrText(addr_).s);
        }
      } else if (candidate_) {
        reject(nowMs, ok, "no connection or no AC service (ffe0/ffe1/ffe2)");
      } else {
        linkDown(nowMs, ok, "AC unit connect failed");
      }
      break;
    }
    case State::Up:
      if (dropped_.load()) {
        if (candidate_) {
          reject(nowMs, true, "disconnected before a status frame");
        } else {
          linkDown(nowMs, true, "AC unit disconnected");
        }
      } else if (candidate_) {
        if (bridge_->responding(nowMs)) {
          candidate_ = false;
          bound_ = true;
          newBinding_ = true;
          backoff_.reset();
          Serial.printf("AC unit bound: %s %s\n", candName_, AddrText(addr_).s);
        } else if (due(nowMs, verifyUntilMs_)) {
          reject(nowMs, true, "no valid status frame within 5 s");
        }
      }
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
    if (wanted()) return;  // bound some other way meanwhile
    if (!found) {
      Serial.println("no AC unit (KT...) found; is it powered, and free of other apps? 'pair' to retry");
      return;
    }
    addr_ = fromNative(c.addr, c.type);
    std::memcpy(candName_, c.name, sizeof candName_);
    candidate_ = true;
    backoff_.reset();
    nextTryMs_ = nowMs;
    Serial.printf("AC candidate %s %s (RSSI %d dBm): connecting to verify\n", candName_, AddrText(addr_).s, c.rssi);
    return;
  }
  if (wanted() || state_ != State::Idle || job_.load() != Job::None) {
    if (wanted()) scanWanted_ = false;  // a candidate is only ever tried once per 'pair'
    return;
  }
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
