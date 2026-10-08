#include "nvs_store.h"

void NvsStore::begin() { prefs_.begin("rvclevel", false); }

rvc::Settings NvsStore::loadSettings() {
  rvc::Settings d;
  rvc::Settings s;
  s.pitch.outDeg = prefs_.getFloat("p_out", d.pitch.outDeg);
  s.pitch.withinDeg = prefs_.getFloat("p_win", d.pitch.withinDeg);
  s.pitch.zeroDeg = prefs_.getFloat("p_zero", d.pitch.zeroDeg);
  s.roll.outDeg = prefs_.getFloat("r_out", d.roll.outDeg);
  s.roll.withinDeg = prefs_.getFloat("r_win", d.roll.withinDeg);
  s.roll.zeroDeg = prefs_.getFloat("r_zero", d.roll.zeroDeg);
  s.blocks.wheelbaseTenthIn = prefs_.getUShort("wb", d.blocks.wheelbaseTenthIn);
  s.blocks.trackTenthIn = prefs_.getUShort("track", d.blocks.trackTenthIn);
  s.blocks.blockHeight = prefs_.getUChar("blk_h", d.blocks.blockHeight);
  s.blocks.blocksCarried = prefs_.getUChar("blk_n", d.blocks.blocksCarried);
  s.blocks.toleranceHundredthIn = prefs_.getUChar("tol", d.blocks.toleranceHundredthIn);
  return s;
}

uint8_t NvsStore::loadAddress() { return prefs_.getUChar("sa", 0xFF); }

void NvsStore::saveSettings(const rvc::Settings& s) {
  prefs_.putFloat("p_out", s.pitch.outDeg);
  prefs_.putFloat("p_win", s.pitch.withinDeg);
  prefs_.putFloat("p_zero", s.pitch.zeroDeg);
  prefs_.putFloat("r_out", s.roll.outDeg);
  prefs_.putFloat("r_win", s.roll.withinDeg);
  prefs_.putFloat("r_zero", s.roll.zeroDeg);
  prefs_.putUShort("wb", s.blocks.wheelbaseTenthIn);
  prefs_.putUShort("track", s.blocks.trackTenthIn);
  prefs_.putUChar("blk_h", s.blocks.blockHeight);
  prefs_.putUChar("blk_n", s.blocks.blocksCarried);
  prefs_.putUChar("tol", s.blocks.toleranceHundredthIn);
}

void NvsStore::saveAddress(uint8_t sa) { prefs_.putUChar("sa", sa); }
