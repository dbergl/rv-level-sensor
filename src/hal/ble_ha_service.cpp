#include "hal/ble_ha_service.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include <cstring>

#include "hal/nvs_store.h"
#include "hal/pair_window.h"
#include "monocypher.h"

namespace {

constexpr const char* kAuthUuid = "af480102-29e2-47f0-a341-4f1a49271bf4";
constexpr const char* kStatusUuid = "af480103-29e2-47f0-a341-4f1a49271bf4";
constexpr const char* kCommandUuid = "af480104-29e2-47f0-a341-4f1a49271bf4";

class AuthCb : public NimBLECharacteristicCallbacks {
 public:
  explicit AuthCb(BleHaService& s) : s_(s) {}
  void onWrite(NimBLECharacteristic* c, ble_gap_conn_desc* desc) override {
    NimBLEAttValue v = c->getValue();
    s_.onAuthWrite(desc->conn_handle, v.data(), v.length());
  }

 private:
  BleHaService& s_;
};

class CommandCb : public NimBLECharacteristicCallbacks {
 public:
  explicit CommandCb(BleHaService& s) : s_(s) {}
  void onWrite(NimBLECharacteristic* c, ble_gap_conn_desc* desc) override {
    NimBLEAttValue v = c->getValue();
    s_.onCommandWrite(desc->conn_handle, v.data(), v.length());
  }

 private:
  BleHaService& s_;
};

class StatusCb : public NimBLECharacteristicCallbacks {
 public:
  explicit StatusCb(BleHaService& s) : s_(s) {}
  void onRead(NimBLECharacteristic* c, ble_gap_conn_desc* desc) override { s_.onStatusRead(c, desc->conn_handle); }

 private:
  BleHaService& s_;
};

}  // namespace

BleHaService::BleHaService(BleServer& server, ac::Bridge& bridge, NvsStore& store, PairWindow& window)
    : server_(server), bridge_(bridge), store_(store), window_(window), ha_(bridge, fillRandom) {}

void BleHaService::fillRandom(uint8_t* out, size_t n) { esp_fill_random(out, n); }

bool BleHaService::begin() {
  mu_ = xSemaphoreCreateMutex();
  if (mu_ == nullptr) return false;
  if (server_.server() == nullptr) return false;
  uint8_t key[ac::kKeyLen];
  if (store_.loadHaKey(key)) ha_.setKey(key);
  crypto_wipe(key, sizeof key);
  server_.addListener(this);
  NimBLEService* svc = server_.server()->createService(kServiceUuid);
  auth_ = svc->createCharacteristic(kAuthUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  auth_->setCallbacks(new AuthCb(*this));
  status_ = svc->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  status_->setCallbacks(new StatusCb(*this));
  command_ = svc->createCharacteristic(kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  command_->setCallbacks(new CommandCb(*this));
  return svc->start();
}

void BleHaService::enqueue(uint8_t kind, uint16_t h, const uint8_t* d, size_t n) {
  Lock l(mu_);
  if (queued_ == kQueue) return;  // HA waits for each reply; a flood is not HA
  Write& w = queue_[queued_++];
  w.h = h;
  w.kind = kind;
  w.n = static_cast<uint8_t>(n < kMaxWrite ? n : kMaxWrite);
  std::memcpy(w.d, d, w.n);
}

void BleHaService::onDisconnect(uint16_t h) {
  Lock l(mu_);
  size_t k = 0;
  for (size_t i = 0; i < queued_; ++i)
    if (queue_[i].h != h) queue_[k++] = queue_[i];
  queued_ = k;
  if (goneCount_ < blelink::Links::kSlots) {
    gone_[goneCount_++] = h;
  } else {
    goneLost_ = true;
  }
  if (h == readLink_) readLink_ = kNoLink;
}

void BleHaService::onStatusRead(NimBLECharacteristic* c, uint16_t h) {
  Lock l(mu_);
  if (h == readLink_) {
    c->setValue(readStatus_, sizeof readStatus_);
  } else {
    c->setValue(nullptr, 0);  // not authenticated: nothing
  }
}

bool BleHaService::pop(Write& w) {
  Lock l(mu_);
  // A disconnect first: the handle may already belong to a new link with writes queued behind it.
  if (queued_ == 0 || goneCount_ > 0 || goneLost_) return false;
  w = queue_[0];
  --queued_;
  for (size_t i = 0; i < queued_; ++i) queue_[i] = queue_[i + 1];
  return true;
}

void BleHaService::takeDisconnects() {
  uint16_t gone[blelink::Links::kSlots];
  size_t n;
  bool lost;
  {
    Lock l(mu_);
    n = goneCount_;
    std::memcpy(gone, gone_, n * sizeof gone[0]);
    goneCount_ = 0;
    lost = goneLost_;
    goneLost_ = false;
  }
  for (size_t i = 0; i < n; ++i) ha_.onDisconnect(gone[i]);
  if (lost && ha_.sessionLink() != kNoLink) ha_.onDisconnect(ha_.sessionLink());
}

void BleHaService::send(NimBLECharacteristic* c, const ac::HaControl::Out& o) {
  if (o.n == 0 || o.h == kNoLink) return;
  // To that one link: NimBLECharacteristic::notify would go to every subscriber.
  os_mbuf* om = ble_hs_mbuf_from_flat(o.d, static_cast<uint16_t>(o.n));
  if (om != nullptr) ble_gattc_notify_custom(o.h, c->getHandle(), om);  // consumes om
}

void BleHaService::handle(const Write& w, uint32_t nowMs) {
  ac::HaControl::Out out;
  if (w.kind == kCommand) {
    ha_.onCommand(w.h, w.d, w.n, nowMs, out);
    send(command_, out);
    return;
  }
  switch (ha_.onAuth(w.h, w.d, w.n, window_.isOpen(nowMs), out)) {
    case ac::HaControl::Auth::Paired:
      store_.saveHaKey(ha_.key());
      window_.close();
      Serial.println("Home Assistant paired");
      break;
    case ac::HaControl::Auth::Session:
      server_.claim(w.h);  // HA's link: not dropped for idling
      break;
    case ac::HaControl::Auth::Refused:
      if (w.n > 0 && w.d[0] == ac::HaControl::kPair) Serial.println("Home Assistant pairing refused");
      break;
    default:
      break;
  }
  send(auth_, out);
}

void BleHaService::dropPending() {
  uint16_t h = ha_.takeDrop();
  if (h != kNoLink) server_.server()->disconnect(h);
}

void BleHaService::poll(uint32_t nowMs) {
  takeDisconnects();
  Write w;
  while (pop(w)) handle(w, nowMs);
  dropPending();

  ac::HaControl::Out out;
  while (ha_.nextResult(out)) send(command_, out);
  if (ha_.statusDue(nowMs, out)) send(status_, out);

  uint16_t link = ha_.sessionLink();
  {
    Lock l(mu_);
    readLink_ = goneCount_ > 0 || goneLost_ ? kNoLink : link;  // a pending disconnect may be this link
    if (link != kNoLink) ac::packStatus(bridge_.status(), bridge_.responding(nowMs), readStatus_);
  }
  if (link != logged_) {
    if (link != kNoLink) {
      Serial.printf("Home Assistant session on link %u\n", link);
    } else {
      Serial.println("Home Assistant session ended");
    }
    logged_ = link;
  }
  // Connectable only for someone who can use it: the paired HA, or one pairing now.
  server_.setWantConnectable(BleServer::kWantHa, link == kNoLink && (ha_.paired() || window_.isOpen(nowMs)));
}

void BleHaService::forget() {
  ha_.forget();
  dropPending();
  Lock l(mu_);
  readLink_ = kNoLink;
}
