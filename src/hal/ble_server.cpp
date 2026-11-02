#include "hal/ble_server.h"

#include <NimBLEDevice.h>

#include "hal/ble_bthome.h"

namespace {

class ServerCb : public NimBLEServerCallbacks {
 public:
  explicit ServerCb(BleServer& s) : s_(s) {}
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override { s_.onConnect(desc->conn_handle); }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc* desc) override { s_.onDisconnect(desc->conn_handle); }

 private:
  BleServer& s_;
};

}  // namespace

bool BleServer::begin(BleBthome& adv) {
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

void BleServer::poll() {
  if (adv_ == nullptr) return;
  bool want = want_.load() != 0 && links_.load() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
  if (want != connectable_ || linkChanged_.exchange(false)) {
    adv_->setConnectable(want);  // also restarts advertising, which a connection stops
    connectable_ = want;
  }
}

void BleServer::onConnect(uint16_t h) {
  links_.fetch_add(1);
  for (int i = 0; i < listenerCount_; ++i) listeners_[i]->onConnect(h);
  linkChanged_ = true;
}

void BleServer::onDisconnect(uint16_t h) {
  if (links_.load() > 0) links_.fetch_sub(1);  // only the host task writes links_
  for (int i = 0; i < listenerCount_; ++i) listeners_[i]->onDisconnect(h);
  linkChanged_ = true;
}
