#include "hal/ble_server.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "hal/ble_bthome.h"

// platformio.ini: the AC unit, Home Assistant and an update client, plus the free slot.
static_assert(!cfg::AC_BRIDGE_ENABLED || CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 4,
              "AC builds need CONFIG_BT_NIMBLE_MAX_CONNECTIONS=4");

namespace {

constexpr uint32_t kRetryMs = 1000;  // retry connectable advertising after a fallback

class ServerCb : public NimBLEServerCallbacks {
 public:
  explicit ServerCb(BleServer& s) : s_(s) {}
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override { s_.onConnect(desc->conn_handle); }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc* desc) override { s_.onDisconnect(desc->conn_handle); }

 private:
  BleServer& s_;
};

}  // namespace

BleServer::BleServer() : links_(CONFIG_BT_NIMBLE_MAX_CONNECTIONS, kClaimTimeoutMs) {}

bool BleServer::begin(BleBthome& adv) {
  mu_ = xSemaphoreCreateMutex();
  if (mu_ == nullptr) return false;
  adv_ = &adv;
  NimBLEDevice::setMTU(517);
  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) return false;
  server_->setCallbacks(new ServerCb(*this));
  server_->advertiseOnDisconnect(false);  // poll() owns advertising
  return true;
}

void BleServer::addListener(IBleConnListener* l) {
  if (listenerCount_ < kMaxListeners) listeners_[listenerCount_++] = l;
}

void BleServer::setWantConnectable(uint8_t who, bool want) {
  if (want) {
    want_.fetch_or(who);
  } else {
    want_.fetch_and(static_cast<uint8_t>(~who));
  }
}

void BleServer::setClientLinks(uint8_t n) {
  if (mu_ == nullptr) return;
  {
    Lock l(mu_);
    links_.setClientLinks(n);
  }
  linkChanged_ = true;
}

void BleServer::claim(uint16_t h) {
  if (mu_ == nullptr) return;
  Lock l(mu_);
  links_.claim(h);
}

void BleServer::poll() {
  if (server_ == nullptr || adv_ == nullptr) return;
  uint32_t now = millis();
  uint16_t drop[blelink::Links::kSlots];
  int n = 0;
  bool want;
  {
    Lock l(mu_);
    uint16_t h;
    while (n < blelink::Links::kSlots && (h = links_.toDrop(now)) != blelink::kNoLink) drop[n++] = h;
    want = links_.connectable(want_.load() != 0);
  }
  for (int i = 0; i < n; ++i) server_->disconnect(drop[i]);

  // BleBthome falls back to non-connectable if a connectable start fails; retry now and then.
  bool retry = want && !adv_->connectable() && now - lastTryMs_ >= kRetryMs;
  if (linkChanged_.exchange(false) || want != requested_ || retry) {
    adv_->setConnectable(want);  // also restarts advertising, which a connection stops
    requested_ = want;
    lastTryMs_ = now;
  }
}

void BleServer::onConnect(uint16_t h) {
  bool keep;
  {
    Lock l(mu_);
    keep = links_.onConnect(h, millis());
  }
  if (!keep) server_->disconnect(h);  // past the cap: one slot stays free so NimBLE can advertise
  for (int i = 0; i < listenerCount_; ++i) listeners_[i]->onConnect(h);
  linkChanged_ = true;
}

void BleServer::onDisconnect(uint16_t h) {
  {
    Lock l(mu_);
    links_.onDisconnect(h);
  }
  for (int i = 0; i < listenerCount_; ++i) listeners_[i]->onDisconnect(h);
  linkChanged_ = true;
}
