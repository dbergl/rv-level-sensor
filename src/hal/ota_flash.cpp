#include "hal/ota_flash.h"

#include <esp_system.h>

uint32_t OtaFlash::capacity() const {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

int32_t OtaFlash::begin(uint32_t) {
  part_ = esp_ota_get_next_update_partition(nullptr);
  if (part_ == nullptr) return ESP_ERR_NOT_FOUND;
  // Erase sector by sector as data arrives, so BEGIN answers at once.
  return esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
}

int32_t OtaFlash::write(const uint8_t* data, size_t len) { return esp_ota_write(handle_, data, len); }

int32_t OtaFlash::finish() {
  esp_err_t err = esp_ota_end(handle_);  // validates the image; frees the handle either way
  handle_ = 0;
  if (err != ESP_OK) return err;
  return esp_ota_set_boot_partition(part_);
}

void OtaFlash::abort() {
  if (handle_ != 0) esp_ota_abort(handle_);
  handle_ = 0;
}

bool OtaFlash::runningOnTrial() {
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

uint8_t OtaFlash::runningSlot() {
  return esp_ota_get_running_partition()->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? 0 : 1;
}

void OtaFlash::confirm() { esp_ota_mark_app_valid_cancel_rollback(); }

void OtaFlash::rollBack() {
  esp_ota_mark_app_invalid_rollback_and_reboot();
  esp_restart();  // only reached if there is no other valid image
}
