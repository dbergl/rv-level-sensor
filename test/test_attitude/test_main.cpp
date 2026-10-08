#include <unity.h>

#include <cmath>

#include "attitude.h"
#include "sensor_health.h"

using namespace level;

void setUp() {}
void tearDown() {}

static Vec3 tilt(float pitch, float roll) {
  // Gravity reaction for a coach pitched/rolled by the given angles (small-angle consistent with pitchDeg/rollDeg).
  float p = pitch * 3.14159265f / 180.0f;
  float r = roll * 3.14159265f / 180.0f;
  float x = std::sin(p);
  float y = std::sin(r);
  float z = std::sqrt(1.0f - x * x - y * y);
  return {x, y, z};
}

void test_identity_map() {
  Vec3 c = mapToCoach({0.1f, 0.2f, 0.97f}, {Axis::PosX, Axis::PosY});
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, c.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.2f, c.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.97f, c.z);
}

void test_rotated_map() {
  // Sensor -Y points forward, +X points to the driver side, so up is +Z.
  Vec3 c = mapToCoach({0.1f, 0.2f, 0.97f}, {Axis::NegY, Axis::PosX});
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.2f, c.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, c.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.97f, c.z);
}

void test_vertical_mount_map() {
  // Board standing on edge: +X forward, +Z driver side, so up is -Y.
  Vec3 c = mapToCoach({0.0f, -1.0f, 0.0f}, {Axis::PosX, Axis::PosZ});
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, c.z);
}

void test_axis_map_validation() {
  TEST_ASSERT_TRUE(isValidAxisMap(Axis::PosX, Axis::PosY));
  TEST_ASSERT_FALSE(isValidAxisMap(Axis::PosX, Axis::PosX));
  TEST_ASSERT_FALSE(isValidAxisMap(Axis::PosX, Axis::NegX));
}

void test_pitch_roll_signs() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, pitchDeg(tilt(2.0f, 0)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, rollDeg(tilt(0, -3.0f)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pitchDeg({0, 0, 1}));
}

void test_filter_seeds_on_first_sample() {
  AttitudeFilter f(1.0f);
  TEST_ASSERT_FALSE(f.seeded());
  f.update(tilt(1.5f, -0.5f), {}, 0);
  TEST_ASSERT_TRUE(f.seeded());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, f.pitchDeg());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f, f.rollDeg());
}

void test_filter_step_response() {
  AttitudeFilter f(1.0f);
  f.update(tilt(0, 0), {}, 0);
  uint32_t t = 0;
  for (int i = 0; i < 100; ++i) f.update(tilt(2.0f, 0), {}, t += 10);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.26f, f.pitchDeg());  // ~63% after one time constant
  for (int i = 0; i < 400; ++i) f.update(tilt(2.0f, 0), {}, t += 10);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 2.0f, f.pitchDeg());
}

void test_filter_motion_window() {
  AttitudeFilter f(1.0f, 0.5f, 1000);
  f.update(tilt(0, 0), {0.1f, 0, 0}, 0);
  TEST_ASSERT_FALSE(f.moving(0));
  f.update(tilt(0, 0), {0, 1.0f, 0}, 100);
  TEST_ASSERT_TRUE(f.moving(100));
  TEST_ASSERT_TRUE(f.moving(1099));
  TEST_ASSERT_FALSE(f.moving(1100));
}

void test_filter_reset() {
  AttitudeFilter f(1.0f);
  f.update(tilt(3.0f, 0), {0, 0, 5.0f}, 0);
  f.reset();
  TEST_ASSERT_FALSE(f.seeded());
  TEST_ASSERT_FALSE(f.moving(10));
  f.update(tilt(-1.0f, 0), {}, 20);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, f.pitchDeg());
}

void test_gyro_bias_calibrates_then_corrects() {
  GyroBias b(4);
  for (int i = 0; i < 3; ++i) b.correct({2.0f, -1.0f, 0.5f});
  TEST_ASSERT_FALSE(b.ready());
  b.correct({2.0f, -1.0f, 0.5f});
  TEST_ASSERT_TRUE(b.ready());
  Vec3 c = b.correct({2.1f, -1.0f, 0.5f});
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f, c.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, c.y);
}

void test_gyro_bias_tracks_slow_drift_only_when_still() {
  GyroBias b(1, 0.5f, 0.1f);
  b.correct({0, 0, 0});
  for (int i = 0; i < 100; ++i) b.correct({0.3f, 0, 0});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.correct({0.3f, 0, 0}).x);  // drift absorbed
  Vec3 fast = b.correct({10.3f, 0, 0});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, fast.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, b.correct({10.3f, 0, 0}).x);  // real motion not absorbed
}

void test_gyro_bias_restarts_capture_when_moving() {
  GyroBias b(4, 0.5f, 0.001f, 0.3f);
  b.correct({0, 0, 0});
  b.correct({2.0f, 0, 0});  // rotation during capture
  b.correct({0, 0, 0});
  b.correct({0, 0, 0});
  TEST_ASSERT_FALSE(b.ready());  // window rejected
  for (int i = 0; i < 4; ++i) b.correct({0.1f, 0, 0});
  TEST_ASSERT_TRUE(b.ready());
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, b.correct({0.1f, 0, 0}).x);
}

void test_health_faults_after_three_failures() {
  SensorHealth h(3, 2000);
  h.onReadFail(0);
  h.onReadFail(10);
  TEST_ASSERT_FALSE(h.faulted());
  h.onReadFail(20);
  TEST_ASSERT_TRUE(h.faulted());
  h.onReadOk();
  TEST_ASSERT_FALSE(h.faulted());
}

void test_health_ok_resets_count() {
  SensorHealth h(3, 2000);
  h.onReadFail(0);
  h.onReadFail(10);
  h.onReadOk();
  h.onReadFail(20);
  TEST_ASSERT_FALSE(h.faulted());
}

void test_health_retry_pacing() {
  SensorHealth h(3, 2000);
  TEST_ASSERT_FALSE(h.shouldRetryInit(5000));  // not faulted
  h.markFaulted(1000);
  TEST_ASSERT_FALSE(h.shouldRetryInit(2999));
  TEST_ASSERT_TRUE(h.shouldRetryInit(3000));
  TEST_ASSERT_FALSE(h.shouldRetryInit(3500));
  TEST_ASSERT_TRUE(h.shouldRetryInit(5000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identity_map);
  RUN_TEST(test_rotated_map);
  RUN_TEST(test_vertical_mount_map);
  RUN_TEST(test_axis_map_validation);
  RUN_TEST(test_pitch_roll_signs);
  RUN_TEST(test_filter_seeds_on_first_sample);
  RUN_TEST(test_filter_step_response);
  RUN_TEST(test_filter_motion_window);
  RUN_TEST(test_filter_reset);
  RUN_TEST(test_gyro_bias_calibrates_then_corrects);
  RUN_TEST(test_gyro_bias_tracks_slow_drift_only_when_still);
  RUN_TEST(test_gyro_bias_restarts_capture_when_moving);
  RUN_TEST(test_health_faults_after_three_failures);
  RUN_TEST(test_health_ok_resets_count);
  RUN_TEST(test_health_retry_pacing);
  return UNITY_END();
}
