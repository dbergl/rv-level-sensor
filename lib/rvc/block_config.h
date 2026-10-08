#pragma once
#include <cstdint>

#include "blocks.h"

namespace rvc {

// LEVEL_CONFIG_COMMAND on proprietary-A2 1EF00h
// (docs/protocol.md).
namespace blockcmd {
constexpr uint8_t SIG0 = 0x4C;  // 'L'
constexpr uint8_t SIG1 = 0x56;  // 'V'
constexpr uint8_t SET_GEOMETRY = 0x01;
constexpr uint8_t SET_BLOCKS = 0x02;
constexpr uint8_t CARRIED_UP = 0x03;
constexpr uint8_t CARRIED_DOWN = 0x04;
constexpr uint8_t RESTORE_DEFAULTS = 0x05;
// Fixed-frame steppers for the panel's LEVEL SETUP page.
constexpr uint8_t HEIGHT_UP = 0x06;     // block height +0.25 in, up to 12.50 in
constexpr uint8_t HEIGHT_DOWN = 0x07;   // block height -0.25 in, down to 0.25 in
constexpr uint8_t TOL_UP = 0x08;        // tolerance +0.05 in from the value in force, up to 2.00 in
constexpr uint8_t TOL_DOWN = 0x09;      // tolerance -0.05 in from the value in force, down to 0
constexpr uint8_t TOL_AUTO = 0x0A;      // tolerance back to auto (half the block height)
// Vehicle geometry steppers for the same page.
constexpr uint8_t WHEELBASE_UP = 0x0B;    // wheelbase +0.5 in, up to 300 in
constexpr uint8_t WHEELBASE_DOWN = 0x0C;  // wheelbase -0.5 in, down to 80 in
constexpr uint8_t TRACK_UP = 0x0D;        // track +0.5 in, up to 110 in
constexpr uint8_t TRACK_DOWN = 0x0E;      // track -0.5 in, down to 40 in
}  // namespace blockcmd

enum class BlockCmdOutcome { Ignored, Applied, Rejected };

struct BlockCmdResult {
  BlockCmdOutcome outcome;
  level::BlockConfig next;  // equals `current` unless Applied
};

// Ignored: under 3 bytes or no "LV" signature (other nodes may use 1EF00h too).
// Rejected, nothing changes: under 8 bytes, unknown opcode, or a value out of range.
// All-ones fields leave the value unchanged; +1/-1 saturate at 250 and 0; the height and
// tolerance and geometry steppers saturate at their range ends.
BlockCmdResult applyBlockConfigCommand(const uint8_t* data, uint8_t len, const level::BlockConfig& current);

}  // namespace rvc
