import pytest

from custom_components.rv_level.levelling import block_layers, corner_lifts, short_on_blocks

WB, TRACK, BLOCK, TOL = 159, 68.5, 1.0, 0.5


def lifts(pitch, roll, wb=WB, track=TRACK, side="left"):
    return corner_lifts(pitch, roll, wb, track, side)


def layers(lift, block=BLOCK, tol=TOL):
    return {c: block_layers(v, block, tol) for c, v in lift.items()}


ROWS = [
    (0, 0, dict(fl=0, fr=0, rl=0, rr=0), dict(fl=0, fr=0, rl=0, rr=0)),
    # front 1 deg low: both fronts come up WB*tan(1) = 2.78 in
    (-1, 0, dict(fl=2.78, fr=2.78, rl=0, rr=0), dict(fl=3, fr=3, rl=0, rr=0)),
    # driver side 2 deg high: passenger side comes up T*tan(2) = 2.39 in -> nearest block, 2
    (0, 2, dict(fl=0, fr=2.39, rl=0, rr=2.39), dict(fl=0, fr=2, rl=0, rr=2)),
    # nose up 1, passenger side up 1: FR is the high corner
    (1, -1, dict(fl=1.2, fr=0, rl=3.97, rr=2.78), dict(fl=1, fr=0, rl=4, rr=3)),
    # under tolerance: lift shown, no blocks
    (-0.08, 0, dict(fl=0.22, fr=0.22, rl=0, rr=0), dict(fl=0, fr=0, rl=0, rr=0)),
]


@pytest.mark.parametrize("pitch,roll,want_lift,want_layers", ROWS)
def test_lift_and_layers(pitch, roll, want_lift, want_layers):
    lift = lifts(pitch, roll)
    assert lift == pytest.approx(want_lift, abs=0.005)
    assert layers(lift) == want_layers


def test_exact_multiple():
    # WB 114.58 -> lift 114.58*tan(1) = 2.00 in; tol 0 -> exactly 2 layers, not 3
    assert layers(lifts(-1, 0, wb=114.58), tol=0) == dict(fl=2, fr=2, rl=0, rr=0)


def test_exact_multiple_fractional_block():
    # lift 2.10 / block 0.7 = 3.0000000000000004 in floating point -> must be 3, not 4
    lift = lifts(-1, 0, wb=120.31)
    assert lift["fl"] == pytest.approx(2.10, abs=0.001)
    assert layers(lift, block=0.7, tol=0) == dict(fl=3, fr=3, rl=0, rr=0)


def test_nearest_block_only_when_it_helps():
    # Roll 0.3 deg lifts the passenger wheels 0.36 in: a 1 in block would overshoot, so none.
    assert layers(lifts(0, 0.3), tol=0)["fr"] == 0
    # Roll 0.45 deg: 0.54 in, just over half a block -> 1.
    assert layers(lifts(0, 0.45), tol=0)["fr"] == 1


def test_half_block_rounds_down():
    # WB 171.87: lift 3.00 in with 2.00 in blocks is exactly 1.5 blocks -> 1.
    lift = lifts(-1, 0, wb=171.87)
    assert lift["fl"] == pytest.approx(3.00, abs=0.005)
    assert layers(lift, block=2.0, tol=0)["fl"] == 1


def test_short_on_blocks():
    assert short_on_blocks(layers(lifts(1, -1)), 10) is False  # 8 layers <= 10 carried
    assert short_on_blocks(layers(lifts(-3, 3)), 10) is True  # 9 + 12 + 0 + 4 = 25 > 10
    assert short_on_blocks(layers(lifts(1, -1)), 7) is True


@pytest.mark.parametrize("pitch,roll", [(1, -1), (0, 2)])
def test_right_hand_drive_mirrors(pitch, roll):
    assert lifts(pitch, roll, side="right") == lifts(pitch, -roll, side="left")


@pytest.mark.parametrize("pitch,roll,_lift,_layers", ROWS)
def test_metric_equals_converted(pitch, roll, _lift, _layers):
    inch = lifts(pitch, roll)
    cm = corner_lifts(pitch, roll, WB * 2.54, TRACK * 2.54, "left")
    assert cm == pytest.approx({c: v * 2.54 for c, v in inch.items()}, abs=0.02)
    assert layers(cm, BLOCK * 2.54, TOL * 2.54) == layers(inch)
