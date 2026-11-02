#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstdint>

#include "ble_links.h"

class BleBthome;
class NimBLEServer;

// Told about every link to the shared server, from the NimBLE host task.
struct IBleConnListener {
  virtual void onConnect(uint16_t h) = 0;
  virtual void onDisconnect(uint16_t h) = 0;
};

// The one NimBLE GATT server, shared by the update service (BleOta) and the Home Assistant
// service. Each service says whether it wants new connections and claims the links that are its
// own; blelink::Links decides which links stay and when the advert may be connectable, so the
// BTHome broadcast never stops. NimBLE calls onConnect/onDisconnect from its host task; poll()
// runs on the loop task and is the only place advertising changes. A mutex guards links_.
class BleServer {
 public:
  static constexpr uint8_t kWantOta = 1;
  static constexpr uint8_t kWantHa = 2;
  static constexpr uint32_t kClaimTimeoutMs = 30000;  // unclaimed links are dropped after this

  BleServer();
  bool begin(BleBthome& adv);  // after BleBthome::begin (NimBLE initialized)
  NimBLEServer* server() { return server_; }
  void addListener(IBleConnListener* l);  // during setup, before any connection
  void setWantConnectable(uint8_t who, bool want);
  void setClientLinks(uint8_t n);  // our own client links (the AC unit): they use host slots too
  void claim(uint16_t h);          // a service owns this link; it is not dropped for idling
  void poll();

  // NimBLE glue, called from the host task.
  void onConnect(uint16_t h);
  void onDisconnect(uint16_t h);

 private:
  static constexpr int kMaxListeners = 2;
  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  BleBthome* adv_ = nullptr;
  NimBLEServer* server_ = nullptr;
  SemaphoreHandle_t mu_ = nullptr;
  blelink::Links links_;
  IBleConnListener* listeners_[kMaxListeners] = {};
  int listenerCount_ = 0;
  std::atomic<uint8_t> want_{0};
  std::atomic<bool> linkChanged_{false};  // a connect or disconnect happened; re-apply advertising
  bool requested_ = false;                // loop task: connectable last asked of BleBthome
  uint32_t lastTryMs_ = 0;
};
