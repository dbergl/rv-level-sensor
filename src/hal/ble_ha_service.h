#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstddef>
#include <cstdint>

#include "bridge.h"
#include "ha_control.h"
#include "hal/ble_server.h"

class NimBLECharacteristic;
class NvsStore;
class PairWindow;

// Home Assistant control service (paired, per-command authenticated) on the shared BleServer:
// auth (0102), status (0103) and command (0104) characteristics. ac::HaControl holds the logic.
// A link becomes HA's session only by proving the pairing key (02 challenge, 03 proof); until then
// it is claimed by nobody, so BleServer drops it after 30 s, and it can't disturb the session.
//
// Tasks: NimBLE calls the on* methods from its host task. They only copy writes into a small
// queue (at most kPerLink per link, so a stranger can't crowd out HA), note subscriptions and
// disconnects; poll() on the loop task does the crypto, talks to the Bridge, stores the key and
// sends notifications, each to one subscribed link. A mutex guards the queue, the subscription
// table and the read cache.
class BleHaService : public IBleConnListener {
 public:
  static constexpr const char* kServiceUuid = "af480101-29e2-47f0-a341-4f1a49271bf4";

  BleHaService(BleServer& server, ac::Bridge& bridge, NvsStore& store, PairWindow& window);
  bool begin();  // after BleServer::begin; loads the pairing key
  void poll(uint32_t nowMs);
  // Unpair: forgets the key in memory and disconnects the session. The caller clears NVS.
  void forget();

  // NimBLE glue, called from the host task.
  static constexpr uint8_t kAuthChr = 1;  // subscription bits
  static constexpr uint8_t kStatusChr = 2;
  static constexpr uint8_t kCommandChr = 4;
  void onConnect(uint16_t) override {}
  void onDisconnect(uint16_t h) override;
  void onAuthWrite(uint16_t h, const uint8_t* d, size_t n) { enqueue(kAuth, h, d, n); }
  void onCommandWrite(uint16_t h, const uint8_t* d, size_t n) { enqueue(kCommand, h, d, n); }
  void onStatusRead(NimBLECharacteristic* c, uint16_t h);
  void onSubscribe(uint16_t h, uint8_t chr, bool on);

 private:
  static constexpr uint16_t kNoLink = ac::HaControl::kNoLink;
  static constexpr uint8_t kAuth = 0;
  static constexpr uint8_t kCommand = 1;
  static constexpr size_t kQueue = 8;
  static constexpr size_t kPerLink = 2;
  static constexpr size_t kLinks = blelink::Links::kSlots;
  // Longer writes are cut to this, which fails every length check (the longest valid is 33).
  static constexpr size_t kMaxWrite = ac::HaControl::kMaxOut;

  struct Write {
    uint16_t h;
    uint8_t kind;
    uint8_t n;
    uint8_t d[kMaxWrite];
  };
  struct Sub {
    uint16_t h;
    uint8_t chrs;
  };
  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  static void fillRandom(uint8_t* out, size_t n);
  static bool fits(uint16_t h, size_t n);  // the link's MTU carries an n-byte notification
  void enqueue(uint8_t kind, uint16_t h, const uint8_t* d, size_t n);
  bool pop(Write& w);  // false when empty or while a disconnect awaits the loop
  void takeDisconnects();
  void handle(const Write& w, uint32_t nowMs);
  void dropPending();
  void setSub(uint16_t h, uint8_t chr, bool on);  // mu_ held
  bool subscribed(uint16_t h, uint8_t chr);
  void send(NimBLECharacteristic* c, uint8_t chr, const ac::HaControl::Out& o);

  BleServer& server_;
  ac::Bridge& bridge_;
  NvsStore& store_;
  PairWindow& window_;
  ac::HaControl ha_;  // loop task only
  NimBLECharacteristic* auth_ = nullptr;
  NimBLECharacteristic* status_ = nullptr;
  NimBLECharacteristic* command_ = nullptr;
  // Loop task only.
  uint16_t logged_ = kNoLink;       // session link last logged
  uint16_t statusSubbed_ = kNoLink;  // session link last seen subscribed to status
  uint16_t mtuWarned_ = kNoLink;     // link last warned about a too-small MTU

  // Host task -> loop, under mu_. A disconnect removes that link's queued writes, so a handle
  // NimBLE reuses for a new link never inherits them; the loop handles disconnects before writes.
  SemaphoreHandle_t mu_ = nullptr;
  Write queue_[kQueue];
  size_t queued_ = 0;
  uint16_t gone_[kLinks];
  size_t goneCount_ = 0;
  bool goneLost_ = false;  // gone_ overflowed: end the session to be safe
  Sub subs_[kLinks] = {};
  size_t subCount_ = 0;
  uint16_t readLink_ = kNoLink;  // the session's link, the only one status reads answer
  uint8_t readStatus_[ac::kStatusLen] = {};
};
