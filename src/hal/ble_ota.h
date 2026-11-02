#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hal/ble_server.h"
#include "ota_session.h"

class OtaFlash;
class NimBLECharacteristic;

// Bluetooth firmware update service (docs/protocol.md), on the shared BleServer.
// Its client is the link that first writes the control characteristic while the window is open;
// writes from any other link are ignored, and closing the window drops only that link.
// NimBLE calls the on* methods from its host task, and loop() calls poll(). A mutex guards the
// session.
class BleOta : public IBleConnListener {
 public:
  BleOta(OtaFlash& flash, BleServer& server, const char* version);
  bool begin();  // after BleServer::begin
  void openWindow(uint32_t nowMs);
  void poll(uint32_t nowMs);  // closes an expired window, drops the client, asks for connections
  bool rebootDue(uint32_t nowMs);
  bool windowOpen();

  // NimBLE glue, called from the host task.
  void onConnect(uint16_t h) override;
  void onDisconnect(uint16_t h) override;
  void onControlWrite(uint16_t h, const uint8_t* d, size_t len);
  void onDataWrite(uint16_t h, const uint8_t* d, size_t len);
  void onInfoRead();

 private:
  static constexpr uint16_t kNoLink = blelink::kNoLink;

  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };
  void notify(const ota::Notice& n);

  BleServer& server_;
  const char* version_;
  ota::Session session_;
  SemaphoreHandle_t mu_ = nullptr;
  NimBLECharacteristic* info_ = nullptr;
  NimBLECharacteristic* control_ = nullptr;
  std::atomic<uint16_t> client_{kNoLink};  // set and cleared on the host task, under mu_
  uint16_t dropped_ = kNoLink;             // loop task: client already asked to disconnect
};
