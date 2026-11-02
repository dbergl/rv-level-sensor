#pragma once
#include <atomic>
#include <cstdint>

class BleBthome;
class NimBLEServer;

// Told about every link to the shared server, from the NimBLE host task.
struct IBleConnListener {
  virtual void onConnect(uint16_t h) = 0;
  virtual void onDisconnect(uint16_t h) = 0;
};

// The one NimBLE GATT server, shared by the update service (BleOta) and the Home Assistant
// service. Each service says whether it wants new connections; the advert is connectable while
// any does and a link is free. NimBLE calls onConnect/onDisconnect from its host task; poll() and
// setWantConnectable() run on the loop task, and only poll() touches advertising.
class BleServer {
 public:
  static constexpr uint8_t kWantOta = 1;
  static constexpr uint8_t kWantHa = 2;

  bool begin(BleBthome& adv);  // after BleBthome::begin (NimBLE initialized)
  NimBLEServer* server() { return server_; }
  void addListener(IBleConnListener* l);  // during setup, before any connection
  void setWantConnectable(uint8_t who, bool want);
  void poll();

  // NimBLE glue, called from the host task.
  void onConnect(uint16_t h);
  void onDisconnect(uint16_t h);

 private:
  static constexpr int kMaxListeners = 2;

  BleBthome* adv_ = nullptr;
  NimBLEServer* server_ = nullptr;
  IBleConnListener* listeners_[kMaxListeners] = {};
  int listenerCount_ = 0;
  std::atomic<uint8_t> want_{0};
  std::atomic<uint8_t> links_{0};
  std::atomic<bool> linkChanged_{false};  // a connect or disconnect happened; re-apply advertising
  bool connectable_ = false;
};
