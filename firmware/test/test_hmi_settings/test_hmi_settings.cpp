#include <unity.h>

#include <string>
#include <vector>

#include "domain.h"
#include "pour.h"
#include "settings.h"

namespace {

bartender::Library library;
bartender::Config config;

bartender::Ingredient ingredientOf(const char* id, const char* name) {
  bartender::Ingredient ingredient;
  ingredient.id = id;
  ingredient.name = name;
  return ingredient;
}

bartender::Slot slotOf(int index, const char* ingredientId, bool empty) {
  bartender::Slot slot;
  slot.index = index;
  if (ingredientId) slot.ingredientId = ingredientId;
  slot.empty = empty;
  return slot;
}

const bartender::Slot* slotAt(const bartender::Config& from, int index) {
  for (const auto& slot : from.slots) {
    if (slot.index == index) return &slot;
  }
  return nullptr;
}

void test_the_choice_list_leads_with_not_assigned_and_sorts_the_rest_by_name() {
  const hmi::SlotChoices choices = hmi::slotChoices(library);
  TEST_ASSERT_EQUAL_STRING("Not assigned\nCampari\ngin\nWhite rum", choices.options.c_str());
  TEST_ASSERT_EQUAL_size_t(4, choices.ids.size());
  TEST_ASSERT_EQUAL_STRING("", choices.ids[0].c_str());
  TEST_ASSERT_EQUAL_STRING("campari", choices.ids[1].c_str());
  TEST_ASSERT_EQUAL_STRING("gin", choices.ids[2].c_str());
  TEST_ASSERT_EQUAL_STRING("rum", choices.ids[3].c_str());
}

void test_an_ingredient_with_no_name_falls_back_to_its_id() {
  bartender::Library sparse;
  sparse.ingredients.push_back(ingredientOf("tonic", ""));
  const hmi::SlotChoices choices = hmi::slotChoices(sparse);
  TEST_ASSERT_EQUAL_STRING("Not assigned\ntonic", choices.options.c_str());
}

void test_the_selected_row_is_the_slots_own_ingredient_and_zero_when_it_has_none() {
  const hmi::SlotChoices choices = hmi::slotChoices(library);
  TEST_ASSERT_EQUAL_INT(2, hmi::choiceOf(choices, std::string("gin")));
  TEST_ASSERT_EQUAL_INT(0, hmi::choiceOf(choices, std::nullopt));
  TEST_ASSERT_EQUAL_INT(0, hmi::choiceOf(choices, std::string("absinthe")));
}

void test_assigning_a_bottle_takes_it_away_from_the_slot_it_was_in() {
  bartender::Config edited;
  edited.slots.push_back(slotOf(0, "gin", false));
  edited.slots.push_back(slotOf(1, "campari", false));

  hmi::assignSlot(edited, 1, std::string("gin"));

  TEST_ASSERT_FALSE(slotAt(edited, 0)->ingredientId.has_value());
  TEST_ASSERT_EQUAL_STRING("gin", slotAt(edited, 1)->ingredientId->c_str());
}

void test_assigning_a_slot_that_has_no_row_yet_creates_one() {
  bartender::Config edited;
  hmi::assignSlot(edited, 5, std::string("rum"));
  TEST_ASSERT_EQUAL_size_t(1, edited.slots.size());
  TEST_ASSERT_EQUAL_INT(5, edited.slots[0].index);
  TEST_ASSERT_EQUAL_STRING("rum", edited.slots[0].ingredientId->c_str());
}

void test_a_new_bottle_in_a_slot_clears_the_empty_flag_the_old_one_left() {
  bartender::Config edited;
  edited.slots.push_back(slotOf(0, "gin", true));
  hmi::assignSlot(edited, 0, std::string("rum"));
  TEST_ASSERT_FALSE(slotAt(edited, 0)->empty);
}

void test_reassigning_the_same_bottle_leaves_the_empty_flag_alone() {
  bartender::Config edited;
  edited.slots.push_back(slotOf(0, "gin", true));
  hmi::assignSlot(edited, 0, std::string("gin"));
  TEST_ASSERT_TRUE(slotAt(edited, 0)->empty);
}

void test_clearing_a_slot_leaves_the_other_slots_untouched() {
  bartender::Config edited;
  edited.slots.push_back(slotOf(0, "gin", false));
  edited.slots.push_back(slotOf(1, "campari", false));
  hmi::assignSlot(edited, 0, std::nullopt);
  TEST_ASSERT_FALSE(slotAt(edited, 0)->ingredientId.has_value());
  TEST_ASSERT_EQUAL_STRING("campari", slotAt(edited, 1)->ingredientId->c_str());
}

void test_marking_empty_creates_the_row_for_a_channel_the_config_never_listed() {
  bartender::Config edited;
  hmi::markSlotEmpty(edited, 7, true);
  TEST_ASSERT_EQUAL_size_t(1, edited.slots.size());
  TEST_ASSERT_TRUE(slotAt(edited, 7)->empty);
}

void test_the_rate_follows_the_bottle_and_an_unknown_one_reads_zero() {
  library.ingredients[0].mlPerSecond = 3.5f;
  TEST_ASSERT_EQUAL_FLOAT(3.5f, hmi::calibrationOf(library, "gin"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hmi::calibrationOf(library, "absinthe"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hmi::calibrationOf(library, ""));

  hmi::setCalibration(library, "gin", 2.5f);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, hmi::calibrationOf(library, "gin"));
  // A rate that follows the bottle stays with that bottle and no other.
  TEST_ASSERT_EQUAL_FLOAT(4.2f, hmi::calibrationOf(library, "campari"));
}

void test_setting_a_rate_for_a_bottle_that_is_not_in_the_library_adds_nothing() {
  hmi::setCalibration(library, "absinthe", 9.0f);
  TEST_ASSERT_EQUAL_size_t(3, library.ingredients.size());
}

// The one thing on this screen that is easy to get backwards, held down here so
// that "fixing" the inversion fails the build. The dose runs for
// mlToMs(10, mlPerSecond): a glass that came up short needs a LONGER on-time,
// and a longer on-time is a SMALLER ml/s.
void test_more_lengthens_the_pour_and_less_shortens_it() {
  const float start = 4.0f;
  const float more = hmi::nudgeMlPerSecond(start, hmi::Dose::more);
  const float less = hmi::nudgeMlPerSecond(start, hmi::Dose::less);

  TEST_ASSERT_TRUE(more < start);
  TEST_ASSERT_TRUE(less > start);
  TEST_ASSERT_EQUAL_FLOAT(3.9f, more);
  TEST_ASSERT_EQUAL_FLOAT(4.1f, less);

  TEST_ASSERT_TRUE(pour::mlToMs(10.0f, more) > pour::mlToMs(10.0f, start));
  TEST_ASSERT_TRUE(pour::mlToMs(10.0f, less) < pour::mlToMs(10.0f, start));
}

void test_the_rate_cannot_be_nudged_to_zero_or_to_something_absurd() {
  TEST_ASSERT_EQUAL_FLOAT(0.2f, hmi::nudgeMlPerSecond(0.2f, hmi::Dose::more));
  TEST_ASSERT_EQUAL_FLOAT(0.2f, hmi::nudgeMlPerSecond(0.0f, hmi::Dose::more));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, hmi::nudgeMlPerSecond(20.0f, hmi::Dose::less));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, hmi::nudgeMlPerSecond(99.0f, hmi::Dose::less));
}

// The slider drives the same value the buttons do and runs the same way round:
// right is more liquid, which is a smaller ml/s.
void test_the_slider_runs_with_the_glass_not_with_the_rate() {
  TEST_ASSERT_TRUE(hmi::doseSlider(3.0f) > hmi::doseSlider(6.0f));
  TEST_ASSERT_TRUE(hmi::doseSliderRate(hmi::kDoseSliderMax) <
                   hmi::doseSliderRate(hmi::kDoseSliderMin));
  TEST_ASSERT_EQUAL_FLOAT(0.2f, hmi::doseSliderRate(hmi::kDoseSliderMax));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, hmi::doseSliderRate(hmi::kDoseSliderMin));
}

void test_the_slider_position_and_the_rate_are_inverses_of_each_other() {
  for (int tenths = hmi::kDoseSliderMin; tenths <= hmi::kDoseSliderMax; tenths++) {
    const float rate = (float)tenths / 10.0f;
    TEST_ASSERT_EQUAL_FLOAT(rate, hmi::doseSliderRate(hmi::doseSlider(rate)));
  }
}

// One press of MORE has to move the slider one notch towards MORE, or the two
// controls are reading the same value in opposite directions.
void test_a_press_of_more_moves_the_slider_towards_more() {
  const int before = hmi::doseSlider(4.0f);
  const int after = hmi::doseSlider(hmi::nudgeMlPerSecond(4.0f, hmi::Dose::more));
  TEST_ASSERT_EQUAL_INT(before + 1, after);
}

void test_cup_distances_clamp_and_step_by_five_millimetres() {
  TEST_ASSERT_EQUAL_FLOAT(5.0f, hmi::clampCupMm(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(400.0f, hmi::clampCupMm(900.0f));
  TEST_ASSERT_EQUAL_FLOAT(105.0f, hmi::nudgeCupMm(100.0f, 1));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, hmi::nudgeCupMm(5.0f, -1));
}

void test_the_debounce_count_stays_between_one_and_ten() {
  TEST_ASSERT_EQUAL_INT(1, hmi::clampDebounce(0));
  TEST_ASSERT_EQUAL_INT(10, hmi::clampDebounce(99));
  TEST_ASSERT_EQUAL_INT(4, hmi::clampDebounce(4));
}

void test_a_cup_has_to_read_nearer_than_the_empty_tray_by_a_margin() {
  bartender::CupSensorConfig sensor;
  sensor.emptyMm = 100.0f;
  sensor.cupMm = 80.0f;
  TEST_ASSERT_TRUE(hmi::cupThresholdsSane(sensor));
  sensor.cupMm = 98.0f;
  TEST_ASSERT_FALSE(hmi::cupThresholdsSane(sensor));
  sensor.cupMm = 120.0f;
  TEST_ASSERT_FALSE(hmi::cupThresholdsSane(sensor));
}

void test_wifi_credentials_are_refused_before_they_can_strand_the_access_point() {
  TEST_ASSERT_FALSE(hmi::wifiSsidValid(""));
  TEST_ASSERT_FALSE(hmi::wifiSsidValid(std::string(33, 'a')));
  TEST_ASSERT_TRUE(hmi::wifiSsidValid("bartender"));

  TEST_ASSERT_FALSE(hmi::wifiPassphraseValid("short"));
  TEST_ASSERT_FALSE(hmi::wifiPassphraseValid(std::string(64, 'a')));
  TEST_ASSERT_TRUE(hmi::wifiPassphraseValid("longenough"));
}

}  // namespace

void setUp() {
  library = bartender::Library();
  library.ingredients.push_back(ingredientOf("gin", "gin"));
  library.ingredients.push_back(ingredientOf("campari", "Campari"));
  library.ingredients.push_back(ingredientOf("rum", "White rum"));
  config = bartender::Config();
}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_choice_list_leads_with_not_assigned_and_sorts_the_rest_by_name);
  RUN_TEST(test_an_ingredient_with_no_name_falls_back_to_its_id);
  RUN_TEST(test_the_selected_row_is_the_slots_own_ingredient_and_zero_when_it_has_none);
  RUN_TEST(test_assigning_a_bottle_takes_it_away_from_the_slot_it_was_in);
  RUN_TEST(test_assigning_a_slot_that_has_no_row_yet_creates_one);
  RUN_TEST(test_a_new_bottle_in_a_slot_clears_the_empty_flag_the_old_one_left);
  RUN_TEST(test_reassigning_the_same_bottle_leaves_the_empty_flag_alone);
  RUN_TEST(test_clearing_a_slot_leaves_the_other_slots_untouched);
  RUN_TEST(test_marking_empty_creates_the_row_for_a_channel_the_config_never_listed);
  RUN_TEST(test_the_rate_follows_the_bottle_and_an_unknown_one_reads_zero);
  RUN_TEST(test_setting_a_rate_for_a_bottle_that_is_not_in_the_library_adds_nothing);
  RUN_TEST(test_more_lengthens_the_pour_and_less_shortens_it);
  RUN_TEST(test_the_rate_cannot_be_nudged_to_zero_or_to_something_absurd);
  RUN_TEST(test_the_slider_runs_with_the_glass_not_with_the_rate);
  RUN_TEST(test_the_slider_position_and_the_rate_are_inverses_of_each_other);
  RUN_TEST(test_a_press_of_more_moves_the_slider_towards_more);
  RUN_TEST(test_cup_distances_clamp_and_step_by_five_millimetres);
  RUN_TEST(test_the_debounce_count_stays_between_one_and_ten);
  RUN_TEST(test_a_cup_has_to_read_nearer_than_the_empty_tray_by_a_margin);
  RUN_TEST(test_wifi_credentials_are_refused_before_they_can_strand_the_access_point);
  return UNITY_END();
}
