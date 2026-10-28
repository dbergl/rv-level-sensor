"""Lift and block-layer maths.

Heights are relative to the centre; +pitch = nose up, +roll = driver side up.
Wheels can only be raised, so every corner comes up to the highest one.
"""

from math import ceil, radians, tan

CORNERS: tuple[str, ...] = ("fl", "fr", "rl", "rr")


def corner_lifts(
    pitch: float, roll: float, wheelbase: float, track: float, driver_side: str
) -> dict[str, float]:
    """Height each corner must come up, in the units of wheelbase and track."""
    a = wheelbase / 2 * tan(radians(pitch))
    b = track / 2 * tan(radians(roll if driver_side == "left" else -roll))
    h = {"fl": a + b, "fr": a - b, "rl": b - a, "rr": -a - b}
    top = max(h.values())
    return {c: round(top - h[c], 2) for c in CORNERS}


def block_layers(lift: float, block_height: float, tolerance: float) -> int:
    """Nearest whole number of blocks, exactly half rounding down: a block only when it gets closer."""
    if lift <= tolerance:
        return 0
    # round() first, so 3.0000000000000004 blocks is 3 and not 4
    return int(ceil(round(lift / block_height - 0.5, 4)))


def short_on_blocks(layers: dict[str, int], carried: float) -> bool:
    """True when the blocks needed exceed the blocks carried."""
    return sum(layers.values()) > carried
