#include "can_twai.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

bool CanTwai::begin(int txPin, int rxPin) {
  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(txPin), static_cast<gpio_num_t>(rxPin), TWAI_MODE_NORMAL);
  g.tx_queue_len = 32;
  g.rx_queue_len = 64;
  g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  return twai_start() == ESP_OK;
}

bool CanTwai::send(const rvc::CanFrame& frame) {
  if (busOff_) {
    ++dropped_;
    return false;
  }
  twai_message_t m = {};
  m.extd = 1;
  m.identifier = frame.id;
  m.data_length_code = frame.len;
  std::memcpy(m.data, frame.data, frame.len);
  if (twai_transmit(&m, 0) != ESP_OK) {
    ++dropped_;
    return false;
  }
  return true;
}

bool CanTwai::receive(rvc::CanFrame& out) { return receive(out, 0); }

bool CanTwai::receive(rvc::CanFrame& out, uint32_t timeoutMs) {
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(timeoutMs);
  twai_message_t m;
  for (;;) {
    TickType_t waited = xTaskGetTickCount() - start;
    esp_err_t err = twai_receive(&m, waited >= timeout ? 0 : timeout - waited);
    if (err == ESP_ERR_INVALID_STATE) {  // driver not running: still honor the wait
      if (waited < timeout) vTaskDelay(timeout - waited);
      return false;
    }
    if (err != ESP_OK) return false;
    if (!m.extd || m.rtr) continue;
    out = rvc::CanFrame{};  // unused bytes stay FFh
    out.id = m.identifier;
    out.len = m.data_length_code > 8 ? 8 : m.data_length_code;
    std::memcpy(out.data, m.data, out.len);
    return true;
  }
}

bool CanTwai::pollRecovered() {
  uint32_t alerts = 0;
  if (twai_read_alerts(&alerts, 0) != ESP_OK) return false;
  if (alerts & TWAI_ALERT_BUS_OFF) {
    busOff_ = true;
    twai_initiate_recovery();
  }
  if (alerts & TWAI_ALERT_BUS_RECOVERED) {
    busOff_ = false;
    twai_start();
    return true;
  }
  return false;
}
