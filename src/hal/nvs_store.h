#pragma once
#include <Preferences.h>

#include "node.h"

// Persists configuration, the last claimed source address, and the AC bindings (unit address,
// Home Assistant pairing key) in NVS.
class NvsStore : public rvc::ISettingsStore {
 public:
  void begin();
  rvc::Settings loadSettings();  // defaults for missing keys; Node rejects invalid values
  uint8_t loadAddress();         // 0xFF if none saved
  void saveSettings(const rvc::Settings& s) override;
  void saveAddress(uint8_t sa) override;

  bool loadUnitAddress(uint8_t addr[6], uint8_t& type);  // false if none saved
  void saveUnitAddress(const uint8_t addr[6], uint8_t type);
  void clearUnitAddress();
  bool loadHaKey(uint8_t key[32]);  // false if none saved
  void saveHaKey(const uint8_t key[32]);
  void clearHaKey();

 private:
  Preferences prefs_;
};
