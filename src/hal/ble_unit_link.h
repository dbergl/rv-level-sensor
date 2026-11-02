#pragma once
#include <NimBLEAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "bridge.h"
#include "link_policy.h"

class BleServer;
class NimBLEClient;
class NimBLEAdvertisedDeviceCallbacks;

// Bluetooth central link to the CountryModPro unit: connects only to the bound address, subscribes
// to its status notifications (ffe1) and writes commands to ffe2 with response. Writes come only
// from the Bridge, which enforces the allow-list. Reconnects with ac::Backoff.
//
// Tasks: the loop task owns the state machine (poll) and the Bridge. NimBLE's connect and
// discovery calls block until the radio answers, so they run on a small worker task the loop
// kicks. Notifications arrive on the NimBLE host task and reach the bridge through a lock-guarded
// byte ring; a disconnect is flagged the same way. write() queues a GATT write request and
// returns: the response is not waited for, the unit's next status frame is the truth.
class BleUnitLink : public ac::IUnitLink {
 public:
  static constexpr uint32_t kScanSeconds = 10;
  static constexpr uint8_t kConnectTimeoutS = 5;

  explicit BleUnitLink(BleServer& server) : server_(server) {}
  // After BleServer::begin. Binds nothing: main passes the stored address to bindTo.
  bool begin(ac::Bridge& b);
  void bindTo(const NimBLEAddress& a);  // the only unit ever connected to
  void forget();                         // drops the link and the binding
  bool bound() const { return bound_; }
  // Scans kScanSeconds for "KT..." names and binds the strongest, if nothing is bound. Logged.
  void startBindScan(uint32_t nowMs);
  // A unit bound by the scan since the last call, for main to store.
  bool takeNewBinding(uint8_t addr[6], uint8_t& type);
  void poll(uint32_t nowMs);

  bool connected() const override;
  bool write(const uint8_t* d, size_t n) override;

  // NimBLE glue, called from the host task.
  void onNotify(const uint8_t* d, size_t n);
  void onDisconnect() { dropped_ = true; }
  void onScanResult(const char* name, size_t len, const uint8_t addr[6], uint8_t type, int rssi);

 private:
  enum class State : uint8_t { Idle, Reserving, Connecting, Up };
  enum class Job : uint8_t { None, Running, Ok, Failed };

  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  static void workerMain(void* self);
  bool connectAndSubscribe();  // worker task
  void drain(uint32_t nowMs);
  void pollScan(uint32_t nowMs);
  void linkDown(uint32_t nowMs, const char* why);

  BleServer& server_;
  ac::Bridge* bridge_ = nullptr;
  NimBLEClient* client_ = nullptr;
  NimBLEAdvertisedDeviceCallbacks* scanCb_ = nullptr;
  TaskHandle_t worker_ = nullptr;
  SemaphoreHandle_t mu_ = nullptr;  // guards ring_ and picker_

  // Host task -> loop.
  ac::ByteRing<128> ring_;
  ac::BindPicker picker_;
  std::atomic<bool> dropped_{false};  // the unit link disconnected

  // Loop -> worker -> loop. The loop writes target_ only while no job runs; the worker writes the
  // handles before setting Job::Ok.
  std::atomic<Job> job_{Job::None};
  NimBLEAddress target_;
  uint16_t connHandle_ = 0xFFFF;
  uint16_t writeHandle_ = 0;

  // Loop task only.
  State state_ = State::Idle;
  bool bound_ = false;
  NimBLEAddress addr_;
  ac::Backoff backoff_;
  uint32_t nextTryMs_ = 0;
  bool scanWanted_ = false;
  bool scanning_ = false;
  uint32_t scanStartMs_ = 0;
  bool newBinding_ = false;
};
