#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ota_session.h"

class BleBthome;
class OtaFlash;
class NimBLEServer;
class NimBLECharacteristic;

// Bluetooth firmware update service (docs/protocol.md).
// NimBLE calls the on* methods from its host task, and loop() calls poll(). A mutex guards the
// session. Advertising changes happen only in poll(), on the loop task, like BleBthome::update.
class BleOta {
 public:
  BleOta(OtaFlash& flash, BleBthome& adv, const char* version);
  bool begin();  // after BleBthome::begin (NimBLE initialised)
  void openWindow(uint32_t nowMs);
  void poll(uint32_t nowMs);  // closes an expired window, drops the client, sets advertising
  bool rebootDue(uint32_t nowMs);
  bool windowOpen();

  // NimBLE glue, called from the host task.
  void onConnect(uint16_t connHandle);
  void onDisconnect();
  void onControlWrite(const uint8_t* d, size_t len);
  void onDataWrite(const uint8_t* d, size_t len);
  void onInfoRead();

 private:
  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };
  void notify(const ota::Notice& n);

  BleBthome& adv_;
  const char* version_;
  ota::Session session_;
  SemaphoreHandle_t mu_ = nullptr;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* info_ = nullptr;
  NimBLECharacteristic* control_ = nullptr;
  std::atomic<bool> connected_{false};
  std::atomic<bool> linkChanged_{false};  // a connect or disconnect happened; re-apply advertising
  std::atomic<uint16_t> connHandle_{0};
  bool dropRequested_ = false;
  bool connectable_ = false;
};
