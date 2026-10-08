#pragma once
#include <vector>

#include "frame.h"
#include "rvc_types.h"

// Records transmitted frames. Set `fail` to simulate a full TX queue.
struct FakeBus : rvc::ITransmitter {
  std::vector<rvc::CanFrame> frames;
  bool fail = false;

  bool send(const rvc::CanFrame& f) override {
    if (fail) return false;
    frames.push_back(f);
    return true;
  }
  void clear() { frames.clear(); }

  // Frames whose on-wire DGN (including any destination byte) equals `dgn`.
  std::vector<rvc::CanFrame> withDgn(uint32_t dgn) const {
    std::vector<rvc::CanFrame> out;
    for (const auto& f : frames)
      if (rvc::parseId(f.id).dgn == dgn) out.push_back(f);
    return out;
  }
  size_t count(uint32_t dgn) const { return withDgn(dgn).size(); }
};
