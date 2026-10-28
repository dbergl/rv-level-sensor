from dataclasses import dataclass

DOMAIN = "rv_level"

CONF_WHEELBASE = "wheelbase"
CONF_TRACK = "track"
CONF_DRIVER_SIDE = "driver_side"
CONF_BLOCK_HEIGHT = "block_height"
CONF_TOLERANCE = "tolerance"
CONF_BLOCKS_CARRIED = "blocks_carried"

CONF_DEVICE_ID = "device_id"
CONF_ADDRESS = "address"
CONF_UNIT = "unit"

UNIT_IN = "in"
UNIT_CM = "cm"


@dataclass(frozen=True)
class SettingSpec:
    default: float
    min: float
    max: float
    step: float


SETTINGS: dict[str, dict[str, SettingSpec]] = {
    UNIT_IN: {
        CONF_WHEELBASE: SettingSpec(159, 80, 300, 0.5),
        CONF_TRACK: SettingSpec(68.5, 40, 110, 0.5),
        CONF_BLOCK_HEIGHT: SettingSpec(1.0, 0.25, 4, 0.05),
        CONF_TOLERANCE: SettingSpec(0.5, 0, 2, 0.05),
        CONF_BLOCKS_CARRIED: SettingSpec(10, 0, 50, 1),
    },
    UNIT_CM: {
        CONF_WHEELBASE: SettingSpec(404, 203, 762, 1),
        CONF_TRACK: SettingSpec(174, 102, 279, 1),
        CONF_BLOCK_HEIGHT: SettingSpec(2.5, 0.5, 10, 0.1),
        CONF_TOLERANCE: SettingSpec(1.25, 0, 5, 0.05),
        CONF_BLOCKS_CARRIED: SettingSpec(10, 0, 50, 1),
    },
}

# Countries that drive on the left, so the driver sits on the right.
RHD_COUNTRIES: frozenset[str] = frozenset(
    "AG AI AU BB BD BM BN BS BT BW CK CY DM FJ FK GB GD GG GY HK ID IE IM IN JE JM JP KE KI KN KY LC LK LS "
    "MO MS MT MU MV MW MY MZ NA NP NR NU NZ PG PK PN SB SC SG SH SR SZ TC TH TL TO TT TV TZ UG VC VG VI WS "
    "ZA ZM ZW".split()
)
