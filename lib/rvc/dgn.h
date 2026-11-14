#pragma once
#include <cstdint>

namespace rvc {
namespace dgn {
// Destination-specific DGNs are stored with DGN-Low = 00h; OR in the destination address.
constexpr uint32_t REQUEST = 0x0EA00;
constexpr uint32_t ACK = 0x0E800;
constexpr uint32_t ADDRESS_CLAIM = 0x0EE00;
constexpr uint32_t INITIAL_PACKET = 0x0EC00;
constexpr uint32_t DATA_PACKET = 0x0EB00;
constexpr uint32_t GENERAL_RESET = 0x17F00;
// Global DGNs.
constexpr uint32_t PRODUCT_ID = 0x0FEEB;
constexpr uint32_t DM_RV = 0x1FECA;
constexpr uint32_t LEVELING_SENSOR_STATUS = 0x1FFEB;
constexpr uint32_t PITCH_CONFIG_STATUS = 0x1FDE9;
constexpr uint32_t PITCH_CONFIG_COMMAND = 0x1FDE8;
constexpr uint32_t ROLL_CONFIG_STATUS = 0x1FDEB;
constexpr uint32_t ROLL_CONFIG_COMMAND = 0x1FDEA;
// Air conditioner (Firefly panel, instance 7).
constexpr uint32_t THERMOSTAT_STATUS_1 = 0x1FFE2;
constexpr uint32_t THERMOSTAT_COMMAND_1 = 0x1FEF9;
constexpr uint32_t THERMOSTAT_STATUS_2 = 0x1FEFA;
constexpr uint32_t THERMOSTAT_COMMAND_2 = 0x1FEF8;
constexpr uint32_t THERMOSTAT_AMBIENT_STATUS = 0x1FF9C;
constexpr uint32_t AIR_CONDITIONER_STATUS = 0x1FFE1;
constexpr uint32_t AIR_CONDITIONER_COMMAND = 0x1FFE0;
constexpr uint32_t FIREFLY_AC_OPTIONS = 0x0FFA0;         // proprietary, panel -> AC
constexpr uint32_t FIREFLY_AC_OPTIONS_STATUS = 0x0FFA1;  // proprietary, AC -> panel
// Proprietary, for the Firefly panel's LEVEL page
// (docs/protocol.md).
constexpr uint32_t PROPRIETARY_A2 = 0x1EF00;      // proprietary-A2, destination-specific; carries LEVEL_CONFIG_COMMAND
// The Firefly panel matches the full DGN, destination included, and can't follow a changed
// address: status always goes to, and commands are always accepted at, BFh (the default SA).
constexpr uint8_t LEVEL_FIXED_DA = 0xBF;
constexpr uint32_t LEVEL_WHEEL_STATUS = PROPRIETARY_A2 | LEVEL_FIXED_DA;  // 1EFBFh, four records
}  // namespace dgn

namespace prio {
constexpr uint8_t SENSOR_STATUS = 3;
constexpr uint8_t NORMAL = 6;
}  // namespace prio
}  // namespace rvc
