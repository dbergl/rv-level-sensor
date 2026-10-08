import pytest

from conftest import entity

CORNERS = ["fl", "fr", "rl", "rr"]


def lifts(ha):
    return {c: float(ha.render(entity("sensor", f"RV Level Lift {c.upper()}")["state"])) for c in CORNERS}


def layers(ha):
    return {c: int(ha.render(entity("sensor", f"RV Level Blocks {c.upper()}")["state"])) for c in CORNERS}


def src(pitch, roll, **kw):
    """States as the renamed BTHome entities feed them to the package's aliases."""
    return dict(sensor__rv_level_pitch=str(pitch), sensor__rv_level_roll=str(roll), **kw)


@pytest.mark.parametrize(
    "pitch,roll,want_lift,want_layers",
    [
        (0, 0, dict(fl=0, fr=0, rl=0, rr=0), dict(fl=0, fr=0, rl=0, rr=0)),
        # front 1 deg low: both fronts come up WB*tan(1) = 2.78 in (not the gist's 1.39)
        (-1, 0, dict(fl=2.78, fr=2.78, rl=0, rr=0), dict(fl=3, fr=3, rl=0, rr=0)),
        # driver side 2 deg high: passenger side comes up T*tan(2) = 2.39 in -> nearest block, 2
        (0, 2, dict(fl=0, fr=2.39, rl=0, rr=2.39), dict(fl=0, fr=2, rl=0, rr=2)),
        # nose up 1, passenger side up 1: FR is the high corner
        (1, -1, dict(fl=1.2, fr=0, rl=3.97, rr=2.78), dict(fl=1, fr=0, rl=4, rr=3)),
        # under tolerance: lift shown, no blocks
        (-0.08, 0, dict(fl=0.22, fr=0.22, rl=0, rr=0), dict(fl=0, fr=0, rl=0, rr=0)),
    ],
)
def test_lift_and_layers(ha, pitch, roll, want_lift, want_layers):
    h = ha(**src(pitch, roll))
    assert lifts(h) == pytest.approx(want_lift, abs=0.005)
    assert layers(h) == want_layers


def test_exact_multiple(ha):
    # WB 114.58 -> lift 114.58*tan(1) = 2.00 in; tol 0 -> exactly 2 layers, not 3
    h = ha(**src(-1, 0, input_number__rv_level_wheelbase="114.58", input_number__rv_level_tolerance="0.0"))
    assert layers(h) == dict(fl=2, fr=2, rl=0, rr=0)


def test_short_on_blocks(ha):
    short = entity("binary_sensor", "RV Level Short On Blocks")["state"]
    assert ha(**src(1, -1)).render(short) == "False"  # 8 layers <= 10 carried
    assert ha(**src(-3, 3)).render(short) == "True"   # 9 + 12 + 0 + 4 = 25 > 10
    assert ha(**src(1, -1, input_number__rv_level_blocks_carried="7.0")).render(short) == "True"


@pytest.mark.parametrize("missing", ["sensor__rv_level_pitch", "sensor__rv_level_roll"])
def test_unavailable_makes_derived_unavailable(ha, missing):
    h = ha(**{**src(-1, 0), missing: "unavailable"})
    for c in CORNERS:
        for kind in ("Lift", "Blocks"):
            e = entity("sensor", f"RV Level {kind} {c.upper()}")
            assert h.render(e["availability"]) == "False"
    assert h.render(entity("binary_sensor", "RV Level Short On Blocks")["availability"]) == "False"


def test_unavailable_ble_source_sets_problem(ha):
    problem = entity("binary_sensor", "RV Level Problem")["state"]
    ok = dict(sensor__rv_level_ble_pitch="0.5", sensor__rv_level_ble_roll="0.1",
              binary_sensor__rv_level_ble_problem="off")
    assert ha(**ok).render(problem) == "False"
    assert ha(**{**ok, "sensor__rv_level_ble_roll": "unavailable"}).render(problem) == "True"
    assert ha(**{k: v for k, v in ok.items() if k != "sensor__rv_level_ble_pitch"}).render(problem) == "True"


def test_missing_problem_source_sets_problem(ha):
    # Pitch/roll renamed but the Problem entity not: a fault would otherwise never show.
    problem = entity("binary_sensor", "RV Level Problem")["state"]
    s = dict(sensor__rv_level_ble_pitch="0.5", sensor__rv_level_ble_roll="0.1")
    assert ha(**s).render(problem) == "True"


def test_exact_multiple_fractional_block(ha):
    # lift 2.10 / block 0.7 = 3.0000000000000004 in floating point -> must be 3, not 4
    h = ha(**src(-1, 0, input_number__rv_level_wheelbase="120.31", input_number__rv_level_tolerance="0.0",
                 input_number__rv_level_block_height="0.7"))
    assert lifts(h)["fl"] == pytest.approx(2.10, abs=0.001)
    assert layers(h) == dict(fl=3, fr=3, rl=0, rr=0)


def test_nearest_block_only_when_it_helps(ha):
    # Tolerance 0, so only the rounding decides. Roll 0.3 deg: passenger wheels lift
    # 68.5*tan(0.3) = 0.36 in. A 1 in block would leave them 0.64 in high, so no block
    # (rounding up would have given 1).
    no_tol = dict(input_number__rv_level_tolerance="0.0")
    assert layers(ha(**src(0, 0.3, **no_tol)))["fr"] == 0
    # Roll 0.45 deg: 0.54 in, just over half a block -> 1.
    assert layers(ha(**src(0, 0.45, **no_tol)))["fr"] == 1


def test_half_block_rounds_down(ha):
    # WB 171.87: lift 171.87*tan(1) = 3.00 in with 2.00 in blocks = exactly 1.5 blocks -> 1.
    h = ha(**src(-1, 0, input_number__rv_level_wheelbase="171.87", input_number__rv_level_tolerance="0.0",
                 input_number__rv_level_block_height="2.0"))
    assert lifts(h)["fl"] == pytest.approx(3.00, abs=0.005)
    assert layers(h)["fl"] == 1


def test_problem_follows_ble_flag(ha):
    problem = entity("binary_sensor", "RV Level Problem")["state"]
    s = dict(sensor__rv_level_ble_pitch="0.5", sensor__rv_level_ble_roll="0.1",
             binary_sensor__rv_level_ble_problem="on")
    assert ha(**s).render(problem) == "True"


def test_aliases(ha):
    h = ha(sensor__rv_level_ble_pitch="-1.23", sensor__rv_level_ble_roll="0.5",
           binary_sensor__rv_level_ble_level="on", binary_sensor__rv_level_ble_moving="off")
    assert h.render(entity("sensor", "RV Level Pitch")["state"]) == "-1.2"
    assert h.render(entity("sensor", "RV Level Roll")["state"]) == "0.5"
    assert h.render(entity("binary_sensor", "RV Level Level")["state"]) == "True"
    assert h.render(entity("binary_sensor", "RV Level Moving")["state"]) == "False"


def test_helper_defaults():
    from conftest import package
    helpers = package()["input_number"]
    assert {k: v["initial"] for k, v in helpers.items()} == {
        "rv_level_wheelbase": 159, "rv_level_track": 68.5, "rv_level_block_height": 1.0,
        "rv_level_tolerance": 0.5, "rv_level_blocks_carried": 10,
    }


SOURCE = "{% from 'rv_level.jinja' import rv_level_src %}{{ rv_level_src(role) }}"


def source(h, role):
    return h.env.from_string(SOURCE).render(role=role).strip()


def test_src_finds_default_bthome_names(ha):
    # HA names BTHome entities rv_level_<last 4 of MAC>_<type>; the second rotation gets _2.
    h = ha(sensor__rv_level_3032_rotation="2.1", sensor__rv_level_3032_rotation_2="0.2",
           binary_sensor__rv_level_3032_generic="off", binary_sensor__rv_level_3032_moving="off",
           binary_sensor__rv_level_3032_problem="off")
    assert source(h, "pitch") == "sensor.rv_level_3032_rotation"
    assert source(h, "roll") == "sensor.rv_level_3032_rotation_2"
    assert source(h, "level") == "binary_sensor.rv_level_3032_generic"
    assert source(h, "moving") == "binary_sensor.rv_level_3032_moving"
    assert source(h, "problem") == "binary_sensor.rv_level_3032_problem"
    assert h.render(entity("sensor", "RV Level Roll")["state"]) == "0.2"
    assert h.render(entity("binary_sensor", "RV Level Problem")["state"]) == "False"


def test_src_prefers_renamed_entities(ha):
    h = ha(sensor__rv_level_ble_pitch="1.0", sensor__rv_level_3032_rotation="9.0")
    assert source(h, "pitch") == "sensor.rv_level_ble_pitch"
    assert h.render(entity("sensor", "RV Level Pitch")["state"]) == "1.0"


def test_src_ignores_other_bthome_devices(ha):
    h = ha(bthome=["sensor.kitchen_ab12_rotation", "sensor.rv_level_3032_rotation_3",
                   "sensor.rv_level_3032_rotation_2"])
    assert source(h, "pitch") == "sensor.rv_level_ble_pitch"  # not found: renamed ID, no state
    assert source(h, "roll") == "sensor.rv_level_3032_rotation_2"


def test_no_sensor_sets_problem(ha):
    h = ha(bthome=[])
    assert h.render(entity("binary_sensor", "RV Level Problem")["state"]) == "True"
    assert h.render(entity("sensor", "RV Level Pitch")["availability"]) == "False"
