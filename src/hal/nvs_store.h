#pragma once
#include <Preferences.h>

#include "node.h"

// Persists configuration and the last claimed source address in NVS.
class NvsStore : public rvc::ISettingsStore {
 public:
  void begin();
  rvc::Settings loadSettings();  // defaults for missing keys; Node rejects invalid values
  uint8_t loadAddress();         // 0xFF if none saved
  void saveSettings(const rvc::Settings& s) override;
  void saveAddress(uint8_t sa) override;

 private:
  Preferences prefs_;
};
