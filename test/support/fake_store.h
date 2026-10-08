#pragma once
#include "node.h"

struct FakeStore : rvc::ISettingsStore {
  rvc::Settings saved;
  int settingsSaves = 0;
  uint8_t address = 0xFF;
  int addressSaves = 0;

  void saveSettings(const rvc::Settings& s) override {
    saved = s;
    ++settingsSaves;
  }
  void saveAddress(uint8_t sa) override {
    address = sa;
    ++addressSaves;
  }
};
