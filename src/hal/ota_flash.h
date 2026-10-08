#pragma once
#include <esp_ota_ops.h>

#include "ota_session.h"

// ota::FlashSink over ESP-IDF's OTA API: writes the spare app slot, then makes it the boot image.
// Also reports and settles the running image's trial state (spec section 2).
class OtaFlash : public ota::FlashSink {
 public:
  uint32_t capacity() const override;
  int32_t begin(uint32_t size) override;
  int32_t write(const uint8_t* data, size_t len) override;
  int32_t finish() override;
  void abort() override;

  static bool runningOnTrial();  // booted from OTA and not yet confirmed (PENDING_VERIFY)
  static uint8_t runningSlot();  // 0 = ota_0 (app0), 1 = ota_1 (app1)
  static void confirm();
  [[noreturn]] static void rollBack();  // marks this image invalid and reboots into the previous one

 private:
  const esp_partition_t* part_ = nullptr;
  esp_ota_handle_t handle_ = 0;
};
