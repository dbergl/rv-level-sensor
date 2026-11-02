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
//
// Tasks: NimBLE calls the on* methods from its host task. They only copy writes into a small
// queue and note disconnects; poll() on the loop task does the crypto, talks to the Bridge, stores
// the key and sends notifications, each to one link. A mutex guards the queue and the read cache.
class BleHaService : public IBleConnListener {
 public:
  static constexpr const char* kServiceUuid = "af480101-29e2-47f0-a341-4f1a49271bf4";

  BleHaService(BleServer& server, ac::Bridge& bridge, NvsStore& store, PairWindow& window);
  bool begin();  // after BleServer::begin; loads the pairing key
  void poll(uint32_t nowMs);
  // Unpair: forgets the key in memory and disconnects the session. The caller clears NVS.
  void forget();

  // NimBLE glue, called from the host task.
  void onConnect(uint16_t) override {}
  void onDisconnect(uint16_t h) override;
  void onAuthWrite(uint16_t h, const uint8_t* d, size_t n) { enqueue(kAuth, h, d, n); }
  void onCommandWrite(uint16_t h, const uint8_t* d, size_t n) { enqueue(kCommand, h, d, n); }
  void onStatusRead(NimBLECharacteristic* c, uint16_t h);

 private:
  static constexpr uint16_t kNoLink = ac::HaControl::kNoLink;
  static constexpr uint8_t kAuth = 0;
  static constexpr uint8_t kCommand = 1;
  static constexpr size_t kQueue = 8;
  // Longer writes are cut to this, which fails every length check (the longest valid is 33).
  static constexpr size_t kMaxWrite = ac::HaControl::kMaxOut;

  struct Write {
    uint16_t h;
    uint8_t kind;
    uint8_t n;
    uint8_t d[kMaxWrite];
  };
  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  static void fillRandom(uint8_t* out, size_t n);
  void enqueue(uint8_t kind, uint16_t h, const uint8_t* d, size_t n);
  bool pop(Write& w);  // false when empty or while a disconnect awaits the loop
  void takeDisconnects();
  void handle(const Write& w, uint32_t nowMs);
  void dropPending();
  void send(NimBLECharacteristic* c, const ac::HaControl::Out& o);

  BleServer& server_;
  ac::Bridge& bridge_;
  NvsStore& store_;
  PairWindow& window_;
  ac::HaControl ha_;  // loop task only
  NimBLECharacteristic* auth_ = nullptr;
  NimBLECharacteristic* status_ = nullptr;
  NimBLECharacteristic* command_ = nullptr;
  uint16_t logged_ = kNoLink;  // loop task: session link last logged

  // Host task -> loop, under mu_. A disconnect removes that link's queued writes, so a handle
  // NimBLE reuses for a new link never inherits them; the loop handles disconnects before writes.
  SemaphoreHandle_t mu_ = nullptr;
  Write queue_[kQueue];
  size_t queued_ = 0;
  uint16_t gone_[blelink::Links::kSlots];
  size_t goneCount_ = 0;
  bool goneLost_ = false;  // gone_ overflowed: end the session to be safe
  uint16_t readLink_ = kNoLink;  // the session's link, the only one status reads answer
  uint8_t readStatus_[ac::kStatusLen] = {};
};
