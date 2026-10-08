#include <unity.h>

#include <string>
#include <vector>

#include "artwork.h"
#include "domain.h"
#include "menu.h"
#include "pourview.h"
#include "ring.h"
#include "screen.h"
#include "theme.h"

namespace {

const char* const kGin = "gin";
const char* const kCampari = "campari";
const char* const kVermouth = "vermouth";
const char* const kRum = "rum";

bartender::Library library;
bartender::Config config;

bartender::Recipe recipeOf(const std::string& id, const std::string& name,
                           const std::vector<bartender::Pour>& pours) {
  bartender::Recipe recipe;
  recipe.id = id;
  recipe.name = name;
  recipe.pours = pours;
  return recipe;
}

bartender::Slot slotOf(int index, const char* ingredientId, bool empty) {
  bartender::Slot slot;
  slot.index = index;
  if (ingredientId) slot.ingredientId = ingredientId;
  slot.empty = empty;
  return slot;
}

hmi::Screen step(hmi::Screen from, hmi::Event event) {
  hmi::Screen next = from;
  hmi::nextScreen(from, event, next);
  return next;
}

bool moves(hmi::Screen from, hmi::Event event) {
  hmi::Screen next = from;
  return hmi::nextScreen(from, event, next);
}

}  // namespace

void setUp() {
  library = bartender::Library();
  library.ingredients = {
      {kGin, "Gin", "#B8A0F0", {}},
      {kCampari, "Campari", "#E04868", {}},
      {kVermouth, "Sweet vermouth", "#C88C40", {}},
      {kRum, "White rum", "#C88C40", {}},
  };
  library.recipes = {
      recipeOf("negroni", "Negroni", {{kGin, 30.0f}, {kCampari, 30.0f}, {kVermouth, 30.0f}}),
      recipeOf("daiquiri", "Daiquiri", {{kRum, 50.0f}}),
  };

  config = bartender::Config();
  config.pumpCount = 8;
  config.slots = {slotOf(0, kGin, false), slotOf(1, kCampari, false),
                  slotOf(2, kVermouth, false), slotOf(3, nullptr, false)};
  config.pin = "1234";
  config.pour.concurrency = 2;
  config.pour.pauseWindowSeconds = 30;
}

void tearDown() {}

// ---------------------------------------------------------------- theme

// Every hex in the design lands on an RGB565 word already, so quantising one is
// the identity. If this fails, a fill will dither at its edges.
void test_design_palette_survives_565_truncation_unchanged() {
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kBase, hmi::quantise565(hmi::colour::kBase));
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kAccent, hmi::quantise565(hmi::colour::kAccent));
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kDanger, hmi::quantise565(hmi::colour::kDanger));
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kUnavailable, hmi::quantise565(hmi::colour::kUnavailable));
  for (int i = 0; i < hmi::kSlotHueCount; i++) {
    TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[i], hmi::quantise565(hmi::kSlotHues[i]));
  }
}

void test_quantise_snaps_an_off_grid_colour_onto_a_565_word() {
  TEST_ASSERT_EQUAL_UINT32(0x909490u, hmi::quantise565(0x949595));
  TEST_ASSERT_EQUAL_UINT32(hmi::quantise565(0x94C7C1), hmi::quantise565(hmi::quantise565(0x94C7C1)));
}

void test_parse_hex_accepts_both_spellings_and_rejects_junk() {
  uint32_t value = 0;
  TEST_ASSERT_TRUE(hmi::parseHex("#38A8F8", value));
  TEST_ASSERT_EQUAL_UINT32(0x38A8F8u, value);
  TEST_ASSERT_TRUE(hmi::parseHex("38a8f8", value));
  TEST_ASSERT_EQUAL_UINT32(0x38A8F8u, value);
  TEST_ASSERT_FALSE(hmi::parseHex("#38A8F", value));
  TEST_ASSERT_FALSE(hmi::parseHex("#38A8FG", value));
  TEST_ASSERT_FALSE(hmi::parseHex("", value));
}

// A loaded slot takes the design hue, not the catalogue colour: the eight are the
// only ones guaranteed to stay out of the accent's blue lane.
void test_a_loaded_slot_takes_its_fixed_hue_over_the_catalogue_colour() {
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[0], hmi::hueFor("#94C7C1", 0));
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[7], hmi::hueFor("", 7));
}

void test_an_unslotted_ingredient_falls_back_to_its_catalogue_colour() {
  TEST_ASSERT_EQUAL_UINT32(hmi::quantise565(0x94C7C1), hmi::hueFor("#94C7C1", -1));
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kLine, hmi::hueFor("not a colour", -1));
}

// ---------------------------------------------------------------- navigation

void test_a_touch_anywhere_leaves_attract_for_the_grid() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid, (int)step(hmi::Screen::attract, hmi::Event::touch));
  TEST_ASSERT_FALSE(moves(hmi::Screen::attract, hmi::Event::cupPlaced));
}

void test_the_guest_spine_runs_grid_to_ready() {
  hmi::Screen at = hmi::Screen::grid;
  at = step(at, hmi::Event::selectDrink);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::detail, (int)at);
  at = step(at, hmi::Event::pourPressed);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::awaitingCup, (int)at);
  at = step(at, hmi::Event::cupPlaced);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::pouring, (int)at);
  at = step(at, hmi::Event::pourComplete);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::ready, (int)at);
  at = step(at, hmi::Event::cupRemoved);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::attract, (int)at);
}

// Ticket 006: a bumped cup pauses and holds the remaining ml, it does not abort.
void test_cup_removed_mid_pour_pauses_and_returning_it_resumes() {
  hmi::Screen at = step(hmi::Screen::pouring, hmi::Event::cupRemoved);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::paused, (int)at);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::pouring, (int)step(at, hmi::Event::cupPlaced));
}

void test_the_pause_window_lapsing_goes_to_aborted() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::aborted,
                        (int)step(hmi::Screen::paused, hmi::Event::pauseLapsed));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::aborted,
                        (int)step(hmi::Screen::paused, hmi::Event::cancel));
}

void test_stop_during_a_pour_goes_to_aborted_not_straight_back_to_the_grid() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::aborted,
                        (int)step(hmi::Screen::pouring, hmi::Event::cancel));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::aborted, hmi::Event::acknowledge));
}

void test_a_fault_takes_over_from_any_guest_screen() {
  const hmi::Screen guest[] = {hmi::Screen::attract, hmi::Screen::grid,     hmi::Screen::detail,
                               hmi::Screen::awaitingCup, hmi::Screen::pouring, hmi::Screen::paused,
                               hmi::Screen::ready};
  for (auto screen : guest) {
    TEST_ASSERT_EQUAL_INT((int)hmi::Screen::fault, (int)step(screen, hmi::Event::faultRaised));
  }
  TEST_ASSERT_FALSE(moves(hmi::Screen::fault, hmi::Event::faultRaised));
}

void test_cancelling_awaiting_cup_returns_to_the_grid() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::awaitingCup, hmi::Event::cancel));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::awaitingCup, hmi::Event::idleTimeout));
}

void test_the_grid_idles_back_to_attract() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::attract,
                        (int)step(hmi::Screen::grid, hmi::Event::idleTimeout));
}

void test_the_gear_is_pin_gated_from_the_grid() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::pinEntry, (int)step(hmi::Screen::grid, hmi::Event::gear));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::settings,
                        (int)step(hmi::Screen::pinEntry, hmi::Event::pinAccepted));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::pinEntry, hmi::Event::cancel));
}

// A stalled pump is not the moment to make someone remember four digits.
void test_settings_from_a_fault_skips_the_pin() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::settings, (int)step(hmi::Screen::fault, hmi::Event::gear));
}

void test_settings_children_are_one_level_deep() {
  hmi::Screen at = step(hmi::Screen::settings, hmi::Event::openSlots);
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::settingsSlots, (int)at);
  TEST_ASSERT_TRUE(hmi::isSettingsChild(at));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::settings, (int)step(at, hmi::Event::back));
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::settings, hmi::Event::closeSettings));
  TEST_ASSERT_FALSE(moves(hmi::Screen::settingsSlots, hmi::Event::openWifi));
}

void test_every_settings_child_is_reachable_from_the_root() {
  const hmi::Event opens[] = {hmi::Event::openSlots,      hmi::Event::openCalibration,
                              hmi::Event::openCupSensor,  hmi::Event::openWifi,
                              hmi::Event::openBrightness, hmi::Event::openConcurrency};
  for (auto open : opens) {
    hmi::Screen child = step(hmi::Screen::settings, open);
    TEST_ASSERT_TRUE(hmi::isSettingsChild(child));
    TEST_ASSERT_EQUAL_INT((int)hmi::Screen::settings, (int)step(child, hmi::Event::back));
  }
}

void test_ready_offers_another_drink_without_going_through_attract() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Screen::grid,
                        (int)step(hmi::Screen::ready, hmi::Event::makeAnother));
}

// ---------------------------------------------------------------- attract

void test_the_attract_cycle_wraps_after_its_dwell() {
  hmi::AttractCycle cycle;
  cycle.setDwellMs(5000);
  cycle.reset({2, 5}, 0);
  TEST_ASSERT_EQUAL_INT(2, cycle.current());
  TEST_ASSERT_FALSE(cycle.advance(4999));
  TEST_ASSERT_TRUE(cycle.advance(5000));
  TEST_ASSERT_EQUAL_INT(5, cycle.current());
  TEST_ASSERT_TRUE(cycle.advance(10000));
  TEST_ASSERT_EQUAL_INT(2, cycle.current());
}

// One pourable drink is a still frame, not a cross-fade: never blit for nothing.
void test_a_single_pourable_drink_never_cross_fades() {
  hmi::AttractCycle cycle;
  cycle.reset({3}, 0);
  TEST_ASSERT_FALSE(cycle.advance(100000));
  TEST_ASSERT_EQUAL_INT(3, cycle.current());
}

void test_an_empty_attract_cycle_advertises_nothing() {
  hmi::AttractCycle cycle;
  cycle.reset({}, 0);
  TEST_ASSERT_TRUE(cycle.empty());
  TEST_ASSERT_EQUAL_INT(-1, cycle.current());
  TEST_ASSERT_FALSE(cycle.advance(100000));
}

// ---------------------------------------------------------------- pin

void test_a_correct_pin_is_accepted_only_once_the_last_digit_lands() {
  hmi::PinEntry pin;
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::incomplete, (int)pin.push('1', config.pin));
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::incomplete, (int)pin.push('2', config.pin));
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::incomplete, (int)pin.push('3', config.pin));
  TEST_ASSERT_EQUAL_INT(3, pin.length());
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::accepted, (int)pin.push('4', config.pin));
  TEST_ASSERT_EQUAL_INT(0, pin.length());
}

void test_a_wrong_pin_is_rejected_and_the_next_digit_starts_over() {
  hmi::PinEntry pin;
  pin.push('9', config.pin);
  pin.push('9', config.pin);
  pin.push('9', config.pin);
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::rejected, (int)pin.push('9', config.pin));
  TEST_ASSERT_TRUE(pin.rejected());
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::incomplete, (int)pin.push('1', config.pin));
  TEST_ASSERT_FALSE(pin.rejected());
  TEST_ASSERT_EQUAL_INT(1, pin.length());
}

void test_backspace_and_non_digits_do_not_disturb_the_entry() {
  hmi::PinEntry pin;
  pin.push('1', config.pin);
  pin.push('2', config.pin);
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::incomplete, (int)pin.push('x', config.pin));
  TEST_ASSERT_EQUAL_INT(2, pin.length());
  pin.backspace();
  TEST_ASSERT_EQUAL_INT(1, pin.length());
  pin.backspace();
  pin.backspace();
  TEST_ASSERT_EQUAL_INT(0, pin.length());
}

// An unset PIN means the machine was never locked; do not strand the owner.
void test_an_unconfigured_pin_opens_the_gate() {
  hmi::PinEntry pin;
  TEST_ASSERT_EQUAL_INT((int)hmi::PinResult::accepted, (int)pin.push('0', ""));
  TEST_ASSERT_EQUAL_INT(4, hmi::PinEntry::boxCount(""));
  TEST_ASSERT_EQUAL_INT(6, hmi::PinEntry::boxCount("123456"));
}

// ---------------------------------------------------------------- menu

void test_available_drinks_sort_first_and_unavailable_last() {
  auto menu = hmi::buildMenu(library, config.slots);
  TEST_ASSERT_EQUAL_size_t(2, menu.size());
  TEST_ASSERT_EQUAL_STRING("Negroni", menu[0].recipe.name.c_str());
  TEST_ASSERT_TRUE(menu[0].available);
  TEST_ASSERT_EQUAL_STRING("Daiquiri", menu[1].recipe.name.c_str());
  TEST_ASSERT_FALSE(menu[1].available);
}

// Alphabetical would have put Daiquiri first; availability outranks the name.
void test_within_a_group_drinks_order_by_name_case_insensitively() {
  library.recipes = {recipeOf("z", "zombie", {{kGin, 10.0f}}),
                     recipeOf("a", "Americano", {{kCampari, 10.0f}}),
                     recipeOf("m", "Mai Tai", {{kGin, 10.0f}})};
  auto menu = hmi::buildMenu(library, config.slots);
  TEST_ASSERT_EQUAL_STRING("Americano", menu[0].recipe.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Mai Tai", menu[1].recipe.name.c_str());
  TEST_ASSERT_EQUAL_STRING("zombie", menu[2].recipe.name.c_str());
}

void test_an_unassigned_ingredient_names_itself_on_the_tile_and_in_the_sheet() {
  auto menu = hmi::buildMenu(library, config.slots);
  TEST_ASSERT_EQUAL_STRING("NEEDS WHITE RUM", menu[1].badge.c_str());
  TEST_ASSERT_EQUAL_STRING("Needs white rum - not in any slot", menu[1].reason.c_str());
}

// The manual empty flag is a different problem from a bottle that was never
// loaded, and naming the slot tells the owner exactly which one to go and refill.
void test_a_slot_flagged_empty_says_which_slot_it_is() {
  config.slots[1].empty = true;
  auto menu = hmi::buildMenu(library, config.slots);
  // Both are unavailable now, so they order by name and Negroni falls second.
  TEST_ASSERT_EQUAL_STRING("Negroni", menu[1].recipe.name.c_str());
  TEST_ASSERT_FALSE(menu[1].available);
  TEST_ASSERT_EQUAL_STRING("NEEDS CAMPARI", menu[1].badge.c_str());
  TEST_ASSERT_EQUAL_STRING("Needs campari - slot 2 is empty", menu[1].reason.c_str());
}

void test_several_missing_bottles_collapse_to_a_count_on_the_tile() {
  config.slots = {slotOf(0, kGin, false)};
  auto menu = hmi::buildMenu(library, config.slots);
  TEST_ASSERT_EQUAL_STRING("Negroni", menu[1].recipe.name.c_str());
  TEST_ASSERT_EQUAL_STRING("NEEDS 2 BOTTLES", menu[1].badge.c_str());
  TEST_ASSERT_EQUAL_STRING("Needs campari and sweet vermouth", menu[1].reason.c_str());
}

void test_an_available_drink_carries_no_badge_and_no_reason() {
  auto menu = hmi::buildMenu(library, config.slots);
  TEST_ASSERT_TRUE(menu[0].badge.empty());
  TEST_ASSERT_TRUE(menu[0].reason.empty());
  TEST_ASSERT_EQUAL_INT('N', menu[0].initial);
}

void test_pourable_indices_are_the_attract_cycle() {
  auto menu = hmi::buildMenu(library, config.slots);
  auto pourable = hmi::pourableIndices(menu);
  TEST_ASSERT_EQUAL_size_t(1, pourable.size());
  TEST_ASSERT_EQUAL_INT(0, pourable[0]);
}

void test_sheet_rows_keep_hues_for_present_ingredients_and_drop_them_for_missing() {
  auto rows = hmi::sheetRows(library.recipes[0], library, config.slots);
  TEST_ASSERT_EQUAL_size_t(3, rows.size());
  TEST_ASSERT_EQUAL_STRING("Gin", rows[0].name.c_str());
  TEST_ASSERT_TRUE(rows[0].present);
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[0], rows[0].hue);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, rows[0].ml);

  auto missing = hmi::sheetRows(library.recipes[1], library, config.slots);
  TEST_ASSERT_FALSE(missing[0].present);
  TEST_ASSERT_EQUAL_UINT32(hmi::colour::kPressed, missing[0].hue);
}

void test_missing_kind_separates_never_loaded_from_flagged_empty() {
  TEST_ASSERT_EQUAL_INT((int)hmi::Missing::none, (int)hmi::missingKind(config.slots, kGin));
  TEST_ASSERT_EQUAL_INT((int)hmi::Missing::notAssigned, (int)hmi::missingKind(config.slots, kRum));
  config.slots[0].empty = true;
  TEST_ASSERT_EQUAL_INT((int)hmi::Missing::slotEmpty, (int)hmi::missingKind(config.slots, kGin));
}

// ---------------------------------------------------------------- pour view

void test_legs_bind_to_the_same_channels_the_scheduler_will_drive() {
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  TEST_ASSERT_EQUAL_size_t(3, legs.size());
  TEST_ASSERT_EQUAL_INT(0, legs[0].channel);
  TEST_ASSERT_EQUAL_STRING("Gin", legs[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[0], legs[0].hue);
  TEST_ASSERT_EQUAL_INT(2, legs[2].channel);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, hmi::totalPlannedMl(legs));
}

// buildPlan drops a pour whose ingredient is not in the library - there is no
// rate to pour it at - so the screen must drop it too, or the cards will
// describe a pump that never runs.
void test_a_pour_with_no_ingredient_behind_it_is_left_out_of_the_view_as_well() {
  library.ingredients.erase(library.ingredients.begin() + 2);
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  TEST_ASSERT_EQUAL_size_t(2, legs.size());
}

void test_an_empty_slot_is_left_out_of_the_view() {
  config.slots[2].empty = true;
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  TEST_ASSERT_EQUAL_size_t(2, legs.size());
}

void test_leg_state_reads_waiting_pouring_and_done_from_delivered_ml_alone() {
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::waiting, (int)hmi::legState(0.0f, 30.0f));
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::pouring, (int)hmi::legState(12.0f, 30.0f));
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::done, (int)hmi::legState(30.0f, 30.0f));
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::done, (int)hmi::legState(0.0f, 0.0f));
}

// Two segments growing at once is how the screen says "two pumps are running"
// without a sentence.
void test_the_bar_segments_by_hue_in_proportion_to_the_whole_drink() {
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  auto bar = hmi::segments(legs, {30.0f, 15.0f, 0.0f});
  TEST_ASSERT_EQUAL_size_t(2, bar.size());
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[0], bar[0].hue);
  TEST_ASSERT_EQUAL_FLOAT(1.0f / 3.0f, bar[0].fraction);
  TEST_ASSERT_EQUAL_FLOAT(1.0f / 6.0f, bar[1].fraction);
}

void test_a_segment_never_overruns_its_own_planned_share() {
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  auto bar = hmi::segments(legs, {45.0f, 0.0f, 0.0f});
  TEST_ASSERT_EQUAL_FLOAT(1.0f / 3.0f, bar[0].fraction);
}

void test_cards_track_each_leg_through_the_pour() {
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  auto deck = hmi::cards(legs, {30.0f, 15.0f, 0.0f});
  TEST_ASSERT_EQUAL_size_t(3, deck.size());
  TEST_ASSERT_EQUAL_STRING("Gin", deck[0].name.c_str());
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::done, (int)deck[0].state);
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::pouring, (int)deck[1].state);
  TEST_ASSERT_EQUAL_INT((int)hmi::LegState::waiting, (int)deck[2].state);
}

// A guest holding a half-made drink wants to know whether it is drinkable, not
// that they received 24 of 30 ml.
void test_quantity_words_bucket_at_95_60_and_25_percent() {
  TEST_ASSERT_EQUAL_STRING("all of it", hmi::quantityWord(1.0f).c_str());
  TEST_ASSERT_EQUAL_STRING("all of it", hmi::quantityWord(0.95f).c_str());
  TEST_ASSERT_EQUAL_STRING("most", hmi::quantityWord(0.94f).c_str());
  TEST_ASSERT_EQUAL_STRING("most", hmi::quantityWord(0.60f).c_str());
  TEST_ASSERT_EQUAL_STRING("about half", hmi::quantityWord(0.59f).c_str());
  TEST_ASSERT_EQUAL_STRING("about half", hmi::quantityWord(0.25f).c_str());
  TEST_ASSERT_EQUAL_STRING("a splash", hmi::quantityWord(0.24f).c_str());
  TEST_ASSERT_EQUAL_STRING("none", hmi::quantityWord(0.0f).c_str());
}

void test_the_abort_ledger_reports_each_leg_in_words() {
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  auto rows = hmi::ledger(legs, {30.0f, 24.0f, 13.5f});
  TEST_ASSERT_EQUAL_size_t(3, rows.size());
  TEST_ASSERT_EQUAL_STRING("all of it", rows[0].quantity.c_str());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, rows[0].fraction);
  TEST_ASSERT_EQUAL_STRING("most", rows[1].quantity.c_str());
  TEST_ASSERT_EQUAL_STRING("about half", rows[2].quantity.c_str());
  TEST_ASSERT_EQUAL_UINT32(hmi::kSlotHues[2], rows[2].hue);
}

void test_the_aborted_screen_names_what_it_nearly_was() {
  TEST_ASSERT_EQUAL_STRING("Part of a Negroni", hmi::abortedTitle("Negroni").c_str());
}

void test_the_pause_bar_drains_over_the_configured_window() {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, hmi::pauseBarFraction(30, 30));
  TEST_ASSERT_EQUAL_FLOAT(0.6f, hmi::pauseBarFraction(18, 30));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hmi::pauseBarFraction(0, 30));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hmi::pauseBarFraction(5, 0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, hmi::pauseBarFraction(40, 30));
}

void test_a_drink_with_no_pourable_legs_produces_an_empty_view() {
  config.slots.clear();
  auto legs = hmi::bindLegs(library.recipes[0], config, library);
  TEST_ASSERT_EQUAL_size_t(0, legs.size());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hmi::totalPlannedMl(legs));
  TEST_ASSERT_EQUAL_size_t(0, hmi::segments(legs, {}).size());
  TEST_ASSERT_EQUAL_size_t(0, hmi::cards(legs, {}).size());
}

void test_square_art_crops_to_the_shape_of_the_box_it_fills() {
  // The grid tile: 240 x 152 out of a 400 x 400 upload, so the crop keeps the
  // full width and takes a band out of the middle.
  auto crop = hmi::coverCrop(400, 400, 240, 152);
  TEST_ASSERT_EQUAL_INT(400, crop.width);
  TEST_ASSERT_EQUAL_INT(253, crop.height);
  TEST_ASSERT_EQUAL_INT(0, crop.x);
  TEST_ASSERT_EQUAL_INT(73, crop.y);

  // A source wider than the box loses its sides instead.
  crop = hmi::coverCrop(800, 480, 300, 240);
  TEST_ASSERT_EQUAL_INT(600, crop.width);
  TEST_ASSERT_EQUAL_INT(480, crop.height);
  TEST_ASSERT_EQUAL_INT(100, crop.x);
  TEST_ASSERT_EQUAL_INT(0, crop.y);
}

void test_a_box_of_the_same_shape_crops_nothing_away() {
  const auto crop = hmi::coverCrop(400, 240, 800, 480);
  TEST_ASSERT_EQUAL_INT(400, crop.width);
  TEST_ASSERT_EQUAL_INT(240, crop.height);
  TEST_ASSERT_EQUAL_INT(0, crop.x);
  TEST_ASSERT_EQUAL_INT(0, crop.y);
}

void test_a_crop_of_nothing_is_empty_rather_than_a_division_by_zero() {
  const auto crop = hmi::coverCrop(0, 400, 240, 0);
  TEST_ASSERT_EQUAL_INT(0, crop.width);
  TEST_ASSERT_EQUAL_INT(0, crop.height);
}

void test_the_ring_breathes_from_dark_through_full_and_back() {
  const uint32_t period = 4000;
  TEST_ASSERT_EQUAL_UINT8(0, hmi::breathLevel(0, period));
  TEST_ASSERT_EQUAL_UINT8(255, hmi::breathLevel(period / 2, period));
  TEST_ASSERT_EQUAL_UINT8(0, hmi::breathLevel(period, period));
  // Every step of the first half climbs, so the ring never stutters mid-swing.
  uint8_t previous = 0;
  for (uint32_t at = 100; at <= period / 2; at += 100) {
    const uint8_t level = hmi::breathLevel(at, period);
    TEST_ASSERT_TRUE(level >= previous);
    previous = level;
  }
  TEST_ASSERT_EQUAL_UINT8(0, hmi::breathLevel(1234, 0));
}

void test_the_ring_is_a_rainbow_while_idle_and_white_once_a_cup_is_wanted() {
  TEST_ASSERT_EQUAL_INT((int)hmi::RingEffect::rainbow,
                        (int)hmi::ringLook(hmi::Screen::attract).effect);
  for (const hmi::Screen screen : {hmi::Screen::grid, hmi::Screen::detail,
                                   hmi::Screen::awaitingCup, hmi::Screen::ready}) {
    const hmi::RingLook look = hmi::ringLook(screen);
    TEST_ASSERT_EQUAL_INT((int)hmi::RingEffect::solid, (int)look.effect);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, look.rgb);
  }
}

void test_a_fault_pulses_red_and_pausing_pulses_faster_than_pouring() {
  const hmi::RingLook fault = hmi::ringLook(hmi::Screen::fault);
  TEST_ASSERT_EQUAL_INT((int)hmi::RingEffect::pulse, (int)fault.effect);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, fault.rgb);

  const hmi::RingLook pouring = hmi::ringLook(hmi::Screen::pouring);
  const hmi::RingLook paused = hmi::ringLook(hmi::Screen::paused);
  TEST_ASSERT_EQUAL_INT((int)hmi::RingEffect::pulse, (int)pouring.effect);
  TEST_ASSERT_EQUAL_INT((int)hmi::RingEffect::pulse, (int)paused.effect);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, pouring.rgb);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, paused.rgb);
  TEST_ASSERT_TRUE(paused.periodMs < pouring.periodMs);
}

void test_a_pulse_dips_without_ever_going_dark() {
  const uint32_t period = 1000;
  TEST_ASSERT_TRUE(hmi::pulseLevel(0, period) > 0);
  TEST_ASSERT_EQUAL_UINT8(255, hmi::pulseLevel(period / 2, period));
  TEST_ASSERT_TRUE(hmi::pulseLevel(0, period) < hmi::pulseLevel(period / 4, period));
}

void test_the_rainbow_spans_the_ring_once_and_turns_a_whole_revolution_per_period() {
  const uint32_t period = 6000;
  const int pixels = 16;
  // Opposite pixels are opposite colours, which is what makes it read as a wheel.
  TEST_ASSERT_EQUAL_UINT32(hmi::wheel(0), hmi::rainbowPixel(0, pixels, 0, period));
  TEST_ASSERT_EQUAL_UINT32(hmi::wheel(128), hmi::rainbowPixel(pixels / 2, pixels, 0, period));
  // A full period later the pattern has come back around to where it started.
  TEST_ASSERT_EQUAL_UINT32(hmi::rainbowPixel(3, pixels, 0, period),
                           hmi::rainbowPixel(3, pixels, period, period));
  // Turning by one pixel's worth of wheel is the same as stepping one pixel back.
  TEST_ASSERT_EQUAL_UINT32(hmi::rainbowPixel(1, pixels, 0, period),
                           hmi::rainbowPixel(0, pixels, period / pixels, period));
  TEST_ASSERT_EQUAL_UINT32(0u, hmi::rainbowPixel(0, 0, 0, period));
}

void test_master_brightness_scales_every_effect_and_zero_means_off() {
  TEST_ASSERT_EQUAL_UINT8(255, hmi::ringLevel(255, 255));
  TEST_ASSERT_EQUAL_UINT8(60, hmi::ringLevel(255, 60));
  TEST_ASSERT_EQUAL_UINT8(30, hmi::ringLevel(128, 60));
  TEST_ASSERT_EQUAL_UINT8(0, hmi::ringLevel(255, 0));
  TEST_ASSERT_EQUAL_UINT8(0, hmi::ringLevel(255, -5));
}

void test_dimming_a_hue_keeps_its_hue() {
  TEST_ASSERT_EQUAL_UINT32(0x804020u, hmi::dim(0xFF8040, 128));
  TEST_ASSERT_EQUAL_UINT32(0u, hmi::dim(0xFF8040, 0));
  TEST_ASSERT_EQUAL_UINT32(0xFF8040u, hmi::dim(0xFF8040, 255));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_design_palette_survives_565_truncation_unchanged);
  RUN_TEST(test_quantise_snaps_an_off_grid_colour_onto_a_565_word);
  RUN_TEST(test_parse_hex_accepts_both_spellings_and_rejects_junk);
  RUN_TEST(test_a_loaded_slot_takes_its_fixed_hue_over_the_catalogue_colour);
  RUN_TEST(test_an_unslotted_ingredient_falls_back_to_its_catalogue_colour);
  RUN_TEST(test_a_touch_anywhere_leaves_attract_for_the_grid);
  RUN_TEST(test_the_guest_spine_runs_grid_to_ready);
  RUN_TEST(test_cup_removed_mid_pour_pauses_and_returning_it_resumes);
  RUN_TEST(test_the_pause_window_lapsing_goes_to_aborted);
  RUN_TEST(test_stop_during_a_pour_goes_to_aborted_not_straight_back_to_the_grid);
  RUN_TEST(test_a_fault_takes_over_from_any_guest_screen);
  RUN_TEST(test_cancelling_awaiting_cup_returns_to_the_grid);
  RUN_TEST(test_the_grid_idles_back_to_attract);
  RUN_TEST(test_the_gear_is_pin_gated_from_the_grid);
  RUN_TEST(test_settings_from_a_fault_skips_the_pin);
  RUN_TEST(test_settings_children_are_one_level_deep);
  RUN_TEST(test_every_settings_child_is_reachable_from_the_root);
  RUN_TEST(test_ready_offers_another_drink_without_going_through_attract);
  RUN_TEST(test_the_attract_cycle_wraps_after_its_dwell);
  RUN_TEST(test_a_single_pourable_drink_never_cross_fades);
  RUN_TEST(test_an_empty_attract_cycle_advertises_nothing);
  RUN_TEST(test_a_correct_pin_is_accepted_only_once_the_last_digit_lands);
  RUN_TEST(test_a_wrong_pin_is_rejected_and_the_next_digit_starts_over);
  RUN_TEST(test_backspace_and_non_digits_do_not_disturb_the_entry);
  RUN_TEST(test_an_unconfigured_pin_opens_the_gate);
  RUN_TEST(test_available_drinks_sort_first_and_unavailable_last);
  RUN_TEST(test_within_a_group_drinks_order_by_name_case_insensitively);
  RUN_TEST(test_an_unassigned_ingredient_names_itself_on_the_tile_and_in_the_sheet);
  RUN_TEST(test_a_slot_flagged_empty_says_which_slot_it_is);
  RUN_TEST(test_several_missing_bottles_collapse_to_a_count_on_the_tile);
  RUN_TEST(test_an_available_drink_carries_no_badge_and_no_reason);
  RUN_TEST(test_pourable_indices_are_the_attract_cycle);
  RUN_TEST(test_sheet_rows_keep_hues_for_present_ingredients_and_drop_them_for_missing);
  RUN_TEST(test_missing_kind_separates_never_loaded_from_flagged_empty);
  RUN_TEST(test_legs_bind_to_the_same_channels_the_scheduler_will_drive);
  RUN_TEST(test_a_pour_with_no_ingredient_behind_it_is_left_out_of_the_view_as_well);
  RUN_TEST(test_an_empty_slot_is_left_out_of_the_view);
  RUN_TEST(test_leg_state_reads_waiting_pouring_and_done_from_delivered_ml_alone);
  RUN_TEST(test_the_bar_segments_by_hue_in_proportion_to_the_whole_drink);
  RUN_TEST(test_a_segment_never_overruns_its_own_planned_share);
  RUN_TEST(test_cards_track_each_leg_through_the_pour);
  RUN_TEST(test_quantity_words_bucket_at_95_60_and_25_percent);
  RUN_TEST(test_the_abort_ledger_reports_each_leg_in_words);
  RUN_TEST(test_the_aborted_screen_names_what_it_nearly_was);
  RUN_TEST(test_the_pause_bar_drains_over_the_configured_window);
  RUN_TEST(test_a_drink_with_no_pourable_legs_produces_an_empty_view);
  RUN_TEST(test_square_art_crops_to_the_shape_of_the_box_it_fills);
  RUN_TEST(test_a_box_of_the_same_shape_crops_nothing_away);
  RUN_TEST(test_a_crop_of_nothing_is_empty_rather_than_a_division_by_zero);
  RUN_TEST(test_the_ring_breathes_from_dark_through_full_and_back);
  RUN_TEST(test_the_ring_is_a_rainbow_while_idle_and_white_once_a_cup_is_wanted);
  RUN_TEST(test_a_fault_pulses_red_and_pausing_pulses_faster_than_pouring);
  RUN_TEST(test_a_pulse_dips_without_ever_going_dark);
  RUN_TEST(test_the_rainbow_spans_the_ring_once_and_turns_a_whole_revolution_per_period);
  RUN_TEST(test_master_brightness_scales_every_effect_and_zero_means_off);
  RUN_TEST(test_dimming_a_hue_keeps_its_hue);
  return UNITY_END();
}
