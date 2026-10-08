#include <unity.h>

#include "cup.h"
#include "domain.h"

namespace {

bartender::CupSensorConfig sensorConfig(int debounceSamples = 1, bool enabled = true) {
  bartender::CupSensorConfig config;
  config.enabled = enabled;
  config.emptyMm = 95.0f;
  config.cupMm = 45.0f;
  config.debounceSamples = debounceSamples;
  return config;
}

void feed(cup::Detector& detector, const cup::Reading& sample, int times) {
  for (int i = 0; i < times; i++) detector.update(sample);
}

}  // namespace

void test_thresholds_are_usable_when_a_cup_reads_nearer_than_the_tray() {
  TEST_ASSERT_TRUE(cup::thresholdsUsable(sensorConfig()));
}

void test_inverted_thresholds_are_unusable() {
  auto config = sensorConfig();
  config.cupMm = 95.0f;
  config.emptyMm = 45.0f;
  TEST_ASSERT_FALSE(cup::thresholdsUsable(config));
}

void test_thresholds_closer_than_the_minimum_separation_are_unusable() {
  auto config = sensorConfig();
  config.emptyMm = 48.0f;
  TEST_ASSERT_FALSE(cup::thresholdsUsable(config));
}

void test_unset_thresholds_are_unusable() {
  bartender::CupSensorConfig config;
  config.enabled = true;
  TEST_ASSERT_FALSE(cup::thresholdsUsable(config));
}

void test_thresholds_beyond_sensor_range_are_unusable() {
  auto config = sensorConfig();
  config.emptyMm = 9000.0f;
  TEST_ASSERT_FALSE(cup::thresholdsUsable(config));
}

void test_a_near_reading_is_a_cup() {
  const auto seen = cup::classify(cup::ranged(45.0f), sensorConfig(), cup::Presence::unknown);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present, (int)seen);
}

void test_a_tray_reading_is_no_cup() {
  const auto seen = cup::classify(cup::ranged(95.0f), sensorConfig(), cup::Presence::unknown);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::absent, (int)seen);
}

void test_a_reading_in_the_hysteresis_band_holds_what_was_believed() {
  const auto config = sensorConfig();
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present,
                        (int)cup::classify(cup::ranged(70.0f), config, cup::Presence::present));
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::absent,
                        (int)cup::classify(cup::ranged(70.0f), config, cup::Presence::absent));
}

void test_a_failed_reading_is_unknown_not_absent() {
  const auto seen = cup::classify(cup::failed(), sensorConfig(), cup::Presence::present);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)seen);
}

void test_a_zero_reading_is_unknown() {
  const auto seen = cup::classify(cup::ranged(0.0f), sensorConfig(), cup::Presence::present);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)seen);
}

void test_an_out_of_range_reading_is_unknown() {
  const auto seen = cup::classify(cup::ranged(8190.0f), sensorConfig(), cup::Presence::present);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)seen);
}

void test_unusable_thresholds_classify_every_reading_as_unknown() {
  auto config = sensorConfig();
  config.cupMm = 95.0f;
  config.emptyMm = 45.0f;
  const auto seen = cup::classify(cup::ranged(45.0f), config, cup::Presence::unknown);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)seen);
}

void test_debounce_needs_consecutive_agreeing_samples_to_commit() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::ranged(45.0f), 2);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  detector.update(cup::ranged(45.0f));
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present, (int)detector.presence());
}

void test_one_disagreeing_sample_restarts_the_debounce_count() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::ranged(45.0f), 2);
  detector.update(cup::ranged(95.0f));
  feed(detector, cup::ranged(45.0f), 2);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  detector.update(cup::ranged(45.0f));
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present, (int)detector.presence());
}

void test_a_debounce_count_below_one_still_takes_one_sample() {
  cup::Detector detector(sensorConfig(0));
  detector.update(cup::ranged(45.0f));
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present, (int)detector.presence());
}

void test_a_glass_parked_on_the_threshold_does_not_chatter() {
  cup::Detector detector(sensorConfig(2));
  feed(detector, cup::ranged(45.0f), 2);
  TEST_ASSERT_TRUE(detector.cupPresent());
  for (int i = 0; i < 20; i++) {
    detector.update(cup::ranged(i % 2 ? 62.0f : 78.0f));
    TEST_ASSERT_TRUE(detector.cupPresent());
  }
}

void test_a_lifted_cup_reads_absent_after_the_debounce() {
  cup::Detector detector(sensorConfig(2));
  feed(detector, cup::ranged(45.0f), 2);
  detector.update(cup::ranged(95.0f));
  TEST_ASSERT_TRUE(detector.cupPresent());
  detector.update(cup::ranged(95.0f));
  TEST_ASSERT_FALSE(detector.cupPresent());
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::absent, (int)detector.presence());
}

void test_a_single_dropped_reading_does_not_lose_the_cup() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::ranged(45.0f), 3);
  detector.update(cup::failed());
  detector.update(cup::failed());
  TEST_ASSERT_TRUE(detector.cupPresent());
}

void test_a_silent_sensor_fails_closed() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::ranged(45.0f), 3);
  TEST_ASSERT_TRUE(detector.cupPresent());
  feed(detector, cup::failed(), 3);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  TEST_ASSERT_FALSE(detector.cupPresent());
}

void test_a_sensor_that_never_answers_never_arms_a_pour() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::failed(), 50);
  TEST_ASSERT_FALSE(detector.cupPresent());
}

void test_unusable_thresholds_never_arm_a_pour_however_good_the_readings() {
  auto config = sensorConfig(1);
  config.cupMm = 95.0f;
  config.emptyMm = 45.0f;
  cup::Detector detector(config);
  feed(detector, cup::ranged(45.0f), 10);
  TEST_ASSERT_FALSE(detector.cupPresent());
}

void test_the_disable_override_reports_a_cup_with_no_sensor_at_all() {
  cup::Detector detector(sensorConfig(3, false));
  feed(detector, cup::failed(), 10);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  TEST_ASSERT_TRUE(detector.cupPresent());
}

void test_the_disable_override_reports_a_cup_even_over_an_empty_tray() {
  cup::Detector detector(sensorConfig(1, false));
  feed(detector, cup::ranged(95.0f), 5);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::absent, (int)detector.presence());
  TEST_ASSERT_TRUE(detector.cupPresent());
}

void test_reconfiguring_with_the_same_config_keeps_the_debounce_progress() {
  cup::Detector detector(sensorConfig(3));
  feed(detector, cup::ranged(45.0f), 2);
  detector.configure(sensorConfig(3));
  detector.update(cup::ranged(45.0f));
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::present, (int)detector.presence());
}

void test_retuning_the_thresholds_forgets_what_was_believed() {
  cup::Detector detector(sensorConfig(1));
  detector.update(cup::ranged(45.0f));
  TEST_ASSERT_TRUE(detector.cupPresent());
  auto retuned = sensorConfig(1);
  retuned.cupMm = 20.0f;
  detector.configure(retuned);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  TEST_ASSERT_FALSE(detector.cupPresent());
}

void test_a_detector_with_no_thresholds_but_the_override_on_still_pours() {
  cup::Detector detector;
  detector.configure(bartender::CupSensorConfig{});
  feed(detector, cup::ranged(45.0f), 5);
  TEST_ASSERT_EQUAL_INT((int)cup::Presence::unknown, (int)detector.presence());
  TEST_ASSERT_TRUE(detector.cupPresent());
}

void test_an_enabled_sensor_with_no_thresholds_set_refuses_to_pour() {
  bartender::CupSensorConfig config;
  config.enabled = true;
  cup::Detector detector(config);
  feed(detector, cup::ranged(45.0f), 5);
  TEST_ASSERT_FALSE(detector.cupPresent());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_thresholds_are_usable_when_a_cup_reads_nearer_than_the_tray);
  RUN_TEST(test_inverted_thresholds_are_unusable);
  RUN_TEST(test_thresholds_closer_than_the_minimum_separation_are_unusable);
  RUN_TEST(test_unset_thresholds_are_unusable);
  RUN_TEST(test_thresholds_beyond_sensor_range_are_unusable);
  RUN_TEST(test_a_near_reading_is_a_cup);
  RUN_TEST(test_a_tray_reading_is_no_cup);
  RUN_TEST(test_a_reading_in_the_hysteresis_band_holds_what_was_believed);
  RUN_TEST(test_a_failed_reading_is_unknown_not_absent);
  RUN_TEST(test_a_zero_reading_is_unknown);
  RUN_TEST(test_an_out_of_range_reading_is_unknown);
  RUN_TEST(test_unusable_thresholds_classify_every_reading_as_unknown);
  RUN_TEST(test_debounce_needs_consecutive_agreeing_samples_to_commit);
  RUN_TEST(test_one_disagreeing_sample_restarts_the_debounce_count);
  RUN_TEST(test_a_debounce_count_below_one_still_takes_one_sample);
  RUN_TEST(test_a_glass_parked_on_the_threshold_does_not_chatter);
  RUN_TEST(test_a_lifted_cup_reads_absent_after_the_debounce);
  RUN_TEST(test_a_single_dropped_reading_does_not_lose_the_cup);
  RUN_TEST(test_a_silent_sensor_fails_closed);
  RUN_TEST(test_a_sensor_that_never_answers_never_arms_a_pour);
  RUN_TEST(test_unusable_thresholds_never_arm_a_pour_however_good_the_readings);
  RUN_TEST(test_the_disable_override_reports_a_cup_with_no_sensor_at_all);
  RUN_TEST(test_the_disable_override_reports_a_cup_even_over_an_empty_tray);
  RUN_TEST(test_reconfiguring_with_the_same_config_keeps_the_debounce_progress);
  RUN_TEST(test_retuning_the_thresholds_forgets_what_was_believed);
  RUN_TEST(test_a_detector_with_no_thresholds_but_the_override_on_still_pours);
  RUN_TEST(test_an_enabled_sensor_with_no_thresholds_set_refuses_to_pour);
  return UNITY_END();
}
