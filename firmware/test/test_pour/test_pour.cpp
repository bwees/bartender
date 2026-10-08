#include <unity.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "domain.h"
#include "pour.h"

class FakeClock : public pour::Clock {
 public:
  uint32_t now = 0;
  uint32_t millis() const override { return now; }
};

class FakePumps : public pour::PumpDriver {
 public:
  std::map<int, bool> channels;
  int allOffCalls = 0;
  int setCalls = 0;
  int nakAtSetCall = -1;

  bool set(int channel, bool on) override {
    if (setCalls++ == nakAtSetCall) return false;
    channels[channel] = on;
    return true;
  }
  bool allOff() override {
    allOffCalls++;
    for (auto& entry : channels) entry.second = false;
    return true;
  }
  bool on(int channel) {
    auto found = channels.find(channel);
    return found != channels.end() && found->second;
  }
  int onCount() {
    int count = 0;
    for (const auto& entry : channels) {
      if (entry.second) count++;
    }
    return count;
  }
};

FakeClock clock_;
FakePumps pumps;

bartender::PourConfig pourConfig(int concurrency) { return {concurrency, 30, 1.5f}; }

bartender::Library libraryOf(const std::vector<std::pair<std::string, float>>& rates) {
  bartender::Library library;
  for (const auto& entry : rates) {
    bartender::Ingredient ingredient;
    ingredient.id = entry.first;
    ingredient.mlPerSecond = entry.second;
    library.ingredients.push_back(ingredient);
  }
  return library;
}

void setUp() {
  clock_ = FakeClock();
  pumps = FakePumps();
}
void tearDown() {}

void test_ml_to_ms_at_calibrated_rate() {
  TEST_ASSERT_EQUAL_UINT32(5000, pour::mlToMs(25.0f, 5.0f));
}

void test_ml_to_ms_rounds_to_nearest() {
  TEST_ASSERT_EQUAL_UINT32(583, pour::mlToMs(3.5f, 6.0f));
}

void test_ml_to_ms_zero_rate_is_zero() {
  TEST_ASSERT_EQUAL_UINT32(0, pour::mlToMs(25.0f, 0.0f));
  TEST_ASSERT_EQUAL_UINT32(0, pour::mlToMs(25.0f, -1.0f));
}

void test_ms_to_ml_round_trip() {
  TEST_ASSERT_EQUAL_FLOAT(25.0f, pour::msToMl(5000, 5.0f));
}

void test_watchdog_cap_scales_calibration() {
  TEST_ASSERT_EQUAL_UINT32(9000, pour::watchdogCapMs(6000, 1.5f, 60000));
}

void test_watchdog_cap_respects_hard_ceiling() {
  TEST_ASSERT_EQUAL_UINT32(10000, pour::watchdogCapMs(60000, 1.5f, 10000));
}

// The rate follows the bottle, so ing-1 pours at 3.0 ml/s in whichever channel
// it is plugged into.
void test_build_plan_times_each_channel_by_its_bottles_own_rate() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}, {"ing-2", 15.0f}};

  bartender::Config config;
  config.slots = {{0, std::optional<std::string>("ing-2"), false},
                  {1, std::optional<std::string>("ing-1"), false}};
  const auto library = libraryOf({{"ing-1", 3.0f}, {"ing-2", 5.0f}});

  auto plan = pour::buildPlan(recipe, config, library);
  TEST_ASSERT_EQUAL_INT(2, (int)plan.size());
  TEST_ASSERT_EQUAL_INT(1, plan[0].channel);
  TEST_ASSERT_EQUAL_UINT32(10000, plan[0].ms);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, plan[0].ml);
  TEST_ASSERT_EQUAL_INT(0, plan[1].channel);
  TEST_ASSERT_EQUAL_UINT32(3000, plan[1].ms);
}

void test_build_plan_follows_the_bottle_when_it_moves_channel() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}};
  const auto library = libraryOf({{"ing-1", 3.0f}});

  bartender::Config first;
  first.slots = {{0, std::optional<std::string>("ing-1"), false}};
  bartender::Config moved;
  moved.slots = {{0, std::nullopt, false}, {5, std::optional<std::string>("ing-1"), false}};

  auto before = pour::buildPlan(recipe, first, library);
  auto after = pour::buildPlan(recipe, moved, library);
  TEST_ASSERT_EQUAL_INT(0, before[0].channel);
  TEST_ASSERT_EQUAL_INT(5, after[0].channel);
  TEST_ASSERT_EQUAL_UINT32(before[0].ms, after[0].ms);
}

void test_build_plan_skips_empty_and_unassigned_slots() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}};

  bartender::Config config;
  config.slots = {{0, std::optional<std::string>("ing-1"), true}, {1, std::nullopt, false}};
  const auto library = libraryOf({{"ing-1", 5.0f}});

  TEST_ASSERT_EQUAL_INT(0, (int)pour::buildPlan(recipe, config, library).size());
}

void test_build_plan_skips_a_pour_whose_ingredient_the_library_has_lost() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}};

  bartender::Config config;
  config.slots = {{0, std::optional<std::string>("ing-1"), false}};

  TEST_ASSERT_EQUAL_INT(0, (int)pour::buildPlan(recipe, config, bartender::Library()).size());
}

void test_deadline_stops_each_channel_at_its_planned_ms() {
  pour::Scheduler scheduler(clock_, pumps);
  TEST_ASSERT_TRUE(scheduler.begin({{0, 1000, 5.0f}, {1, 3000, 15.0f}}, pourConfig(2)));
  TEST_ASSERT_TRUE(pumps.on(0));
  TEST_ASSERT_TRUE(pumps.on(1));

  clock_.now = 1000;
  scheduler.tick();
  TEST_ASSERT_FALSE(pumps.on(0));
  TEST_ASSERT_TRUE(pumps.on(1));
  TEST_ASSERT_EQUAL_UINT32(1000, scheduler.deliveredMs(0));

  clock_.now = 2999;
  scheduler.tick();
  TEST_ASSERT_TRUE(pumps.on(1));
  TEST_ASSERT_EQUAL_INT((int)pour::State::pouring, (int)scheduler.state());

  clock_.now = 3000;
  scheduler.tick();
  TEST_ASSERT_FALSE(pumps.on(1));
  TEST_ASSERT_EQUAL_INT((int)pour::State::complete, (int)scheduler.state());
  TEST_ASSERT_EQUAL_UINT32(3000, scheduler.deliveredMs(1));
}

void test_overshooting_tick_clamps_delivered_to_planned() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 1000, 5.0f}}, pourConfig(1));

  clock_.now = 4000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_UINT32(1000, scheduler.deliveredMs(0));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, scheduler.deliveredMl());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, scheduler.fractionComplete());
}

void test_fraction_complete_is_delivered_over_planned_ml() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 4000, 20.0f}, {1, 2000, 10.0f}}, pourConfig(2));
  TEST_ASSERT_EQUAL_FLOAT(30.0f, scheduler.plannedMl());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, scheduler.fractionComplete());

  clock_.now = 1000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_FLOAT(10.0f, scheduler.deliveredMl());
  TEST_ASSERT_EQUAL_FLOAT(1.0f / 3.0f, scheduler.fractionComplete());

  clock_.now = 4000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_FLOAT(1.0f, scheduler.fractionComplete());
}

void test_pause_preserves_remaining_ml_and_resume_finishes_it() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 4000, 20.0f}, {1, 2000, 10.0f}}, pourConfig(2));

  clock_.now = 1000;
  scheduler.tick();
  TEST_ASSERT_TRUE(scheduler.pause());
  TEST_ASSERT_EQUAL_INT((int)pour::State::paused, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
  TEST_ASSERT_EQUAL_FLOAT(15.0f, scheduler.remainingMl(0));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, scheduler.remainingMl(1));

  clock_.now = 6000;
  TEST_ASSERT_EQUAL_FLOAT(1.0f / 3.0f, scheduler.fractionComplete());
  TEST_ASSERT_TRUE(scheduler.resume());
  TEST_ASSERT_EQUAL_INT(2, pumps.onCount());

  clock_.now = 7000;
  scheduler.tick();
  TEST_ASSERT_FALSE(pumps.on(1));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, scheduler.remainingMl(1));

  clock_.now = 9000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::complete, (int)scheduler.state());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, scheduler.fractionComplete());
}

void test_pause_window_counts_down_and_lapses_to_aborted() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 4000, 20.0f}}, pourConfig(1));

  clock_.now = 1000;
  scheduler.tick();
  scheduler.pause();
  TEST_ASSERT_EQUAL_UINT32(30, scheduler.pauseRemainingSeconds());

  clock_.now = 16000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_UINT32(15, scheduler.pauseRemainingSeconds());
  TEST_ASSERT_EQUAL_INT((int)pour::State::paused, (int)scheduler.state());

  clock_.now = 31000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::aborted, (int)scheduler.state());
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.pauseRemainingSeconds());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
  TEST_ASSERT_EQUAL_FLOAT(5.0f, scheduler.deliveredMl());
}

void test_k_admission_starts_the_next_pour_as_a_slot_frees() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 1000, 5.0f}, {1, 2000, 10.0f}, {2, 500, 2.5f}, {3, 500, 2.5f}},
                  pourConfig(2));
  TEST_ASSERT_EQUAL_INT(2, scheduler.runningCount());
  TEST_ASSERT_FALSE(pumps.on(2));

  clock_.now = 1000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT(2, scheduler.runningCount());
  TEST_ASSERT_FALSE(pumps.on(0));
  TEST_ASSERT_TRUE(pumps.on(1));
  TEST_ASSERT_TRUE(pumps.on(2));
  TEST_ASSERT_FALSE(pumps.on(3));

  clock_.now = 1500;
  scheduler.tick();
  TEST_ASSERT_FALSE(pumps.on(2));
  TEST_ASSERT_TRUE(pumps.on(3));

  clock_.now = 2000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::complete, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
}

void test_concurrency_of_one_is_strictly_sequential() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 1000, 5.0f}, {1, 1000, 5.0f}, {2, 1000, 5.0f}}, pourConfig(1));

  for (uint32_t step = 0; step < 3; step++) {
    TEST_ASSERT_EQUAL_INT(1, scheduler.runningCount());
    TEST_ASSERT_TRUE(pumps.on((int)step));
    clock_.now = (step + 1) * 1000;
    scheduler.tick();
    TEST_ASSERT_FALSE(pumps.on((int)step));
  }
  TEST_ASSERT_EQUAL_INT((int)pour::State::complete, (int)scheduler.state());
}

void test_watchdog_fires_independently_of_the_deadline() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 100000, 500.0f}}, pourConfig(1));
  TEST_ASSERT_EQUAL_UINT32(pour::kWatchdogHardCapMs, scheduler.watchdogCap(0));

  clock_.now = pour::kWatchdogHardCapMs - 1;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::pouring, (int)scheduler.state());
  TEST_ASSERT_TRUE(pumps.on(0));

  clock_.now = pour::kWatchdogHardCapMs;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT((int)pour::Fault::watchdog, (int)scheduler.fault());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
  TEST_ASSERT_TRUE(scheduler.deliveredMs(0) < 100000);
}

void test_watchdog_window_restarts_on_resume() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 500000, 2500.0f}}, pourConfig(1));

  clock_.now = pour::kWatchdogHardCapMs - 1000;
  scheduler.tick();
  scheduler.pause();
  clock_.now += 1000;
  scheduler.resume();

  clock_.now += pour::kWatchdogHardCapMs - 1;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::pouring, (int)scheduler.state());

  clock_.now += 1;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::Fault::watchdog, (int)scheduler.fault());
}

void test_i2c_nak_on_start_turns_everything_off() {
  pour::Scheduler scheduler(clock_, pumps);
  pumps.nakAtSetCall = 1;
  TEST_ASSERT_FALSE(scheduler.begin({{0, 1000, 5.0f}, {1, 1000, 5.0f}}, pourConfig(2)));
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT((int)pour::Fault::i2c, (int)scheduler.fault());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
  TEST_ASSERT_EQUAL_INT(0, scheduler.runningCount());
}

void test_i2c_nak_on_stop_turns_everything_off() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 1000, 5.0f}, {1, 3000, 15.0f}}, pourConfig(2));
  pumps.nakAtSetCall = pumps.setCalls;

  clock_.now = 1000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT((int)pour::Fault::i2c, (int)scheduler.fault());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
  TEST_ASSERT_TRUE(pumps.allOffCalls > 1);
}

void test_faulted_scheduler_ignores_further_ticks() {
  pour::Scheduler scheduler(clock_, pumps);
  pumps.nakAtSetCall = 0;
  scheduler.begin({{0, 1000, 5.0f}}, pourConfig(1));

  clock_.now = 5000;
  scheduler.tick();
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());
  TEST_ASSERT_FALSE(scheduler.pause());
  TEST_ASSERT_FALSE(scheduler.resume());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());
}

// A latched fault is dismissable exactly once per fault: acknowledging clears it
// and it must stay clear, or the screen bounces straight back to the fault.
void test_a_cleared_fault_stays_cleared_until_something_fails_again() {
  pour::Scheduler scheduler(clock_, pumps);
  pumps.nakAtSetCall = 0;
  scheduler.begin({{0, 1000, 5.0f}}, pourConfig(1));
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());

  scheduler.clearFault();
  TEST_ASSERT_EQUAL_INT((int)pour::State::idle, (int)scheduler.state());
  TEST_ASSERT_EQUAL_INT((int)pour::Fault::none, (int)scheduler.fault());
  TEST_ASSERT_EQUAL_INT(0, pumps.onCount());

  for (int i = 0; i < 5; i++) {
    clock_.now += 1000;
    scheduler.tick();
  }
  TEST_ASSERT_EQUAL_INT((int)pour::State::idle, (int)scheduler.state());

  // Still able to fault when the hardware actually fails again.
  pumps.nakAtSetCall = pumps.setCalls;
  TEST_ASSERT_FALSE(scheduler.begin({{0, 1000, 5.0f}}, pourConfig(1)));
  TEST_ASSERT_EQUAL_INT((int)pour::State::fault, (int)scheduler.state());
}

void test_clearing_a_fault_that_never_happened_changes_nothing() {
  pour::Scheduler scheduler(clock_, pumps);
  scheduler.begin({{0, 1000, 5.0f}}, pourConfig(1));
  scheduler.clearFault();
  TEST_ASSERT_EQUAL_INT((int)pour::State::pouring, (int)scheduler.state());
  TEST_ASSERT_TRUE(pumps.on(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ml_to_ms_at_calibrated_rate);
  RUN_TEST(test_ml_to_ms_rounds_to_nearest);
  RUN_TEST(test_ml_to_ms_zero_rate_is_zero);
  RUN_TEST(test_ms_to_ml_round_trip);
  RUN_TEST(test_watchdog_cap_scales_calibration);
  RUN_TEST(test_watchdog_cap_respects_hard_ceiling);
  RUN_TEST(test_build_plan_times_each_channel_by_its_bottles_own_rate);
  RUN_TEST(test_build_plan_follows_the_bottle_when_it_moves_channel);
  RUN_TEST(test_build_plan_skips_a_pour_whose_ingredient_the_library_has_lost);
  RUN_TEST(test_build_plan_skips_empty_and_unassigned_slots);
  RUN_TEST(test_deadline_stops_each_channel_at_its_planned_ms);
  RUN_TEST(test_overshooting_tick_clamps_delivered_to_planned);
  RUN_TEST(test_fraction_complete_is_delivered_over_planned_ml);
  RUN_TEST(test_pause_preserves_remaining_ml_and_resume_finishes_it);
  RUN_TEST(test_pause_window_counts_down_and_lapses_to_aborted);
  RUN_TEST(test_k_admission_starts_the_next_pour_as_a_slot_frees);
  RUN_TEST(test_concurrency_of_one_is_strictly_sequential);
  RUN_TEST(test_watchdog_fires_independently_of_the_deadline);
  RUN_TEST(test_watchdog_window_restarts_on_resume);
  RUN_TEST(test_i2c_nak_on_start_turns_everything_off);
  RUN_TEST(test_i2c_nak_on_stop_turns_everything_off);
  RUN_TEST(test_faulted_scheduler_ignores_further_ticks);
  RUN_TEST(test_a_cleared_fault_stays_cleared_until_something_fails_again);
  RUN_TEST(test_clearing_a_fault_that_never_happened_changes_nothing);
  return UNITY_END();
}
