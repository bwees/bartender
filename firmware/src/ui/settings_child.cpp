#include <cstdio>
#include <string>

#include "../state_lock.h"
#include <cstring>

#include "ui_internal.h"

namespace ui {
namespace {

constexpr uint32_t kHoldGraceMs = 400;
constexpr uint32_t kHoldCeilingMs = 15000;
constexpr int kMaxChannels = 16;
constexpr float kTestMl = 10.0f;

// The body of every child screen: 16 px in from the header rule.
constexpr int kBodyX = kGutter;
constexpr int kBodyY = kHeaderH + kGutter;
constexpr int kBodyW = kScreenW - 2 * kGutter;
constexpr int kBodyH = kScreenH - kHeaderH - 2 * kGutter;

int holdChannel = -1;
uint32_t holdSeenMs = 0;
uint32_t holdStartMs = 0;
// Set only by TEST, which runs a measured dose rather than a dead-man hold.
uint32_t holdUntilMs = 0;
lv_obj_t* holdStatus = nullptr;

int calChannel = 0;
lv_obj_t* doseTrack = nullptr;

lv_obj_t* cupStatus = nullptr;
lv_obj_t* cupEmptyMark = nullptr;
lv_obj_t* cupThreshMark = nullptr;
lv_obj_t* cupTrack = nullptr;
// Held explicitly: at build time the track has not been laid out yet, so querying
// its width returns 0 and both markers land off the left edge until something else
// forces a re-place.
int cupSpan = 0;
int cupTrackX = 0;

constexpr float kCupScaleMm = 400.0f;

int cupBarX(float mm, int span) {
  int x = (int)(mm / kCupScaleMm * (float)span + 0.5f);
  if (x < 0) x = 0;
  if (x > span - 4) x = span - 4;
  return x;
}

// Both markers move with the steppers, so the owner can see the two thresholds
// against the reading they are watching rather than guessing.
void placeCupMarkers() {
  if (!cupEmptyMark || !cupThreshMark || cupSpan <= 0) return;
  const auto& sensor = g.config->cupSensor;
  lv_obj_set_x(cupEmptyMark, cupTrackX + cupBarX(sensor.emptyMm, cupSpan));
  lv_obj_set_x(cupThreshMark, cupTrackX + cupBarX(sensor.cupMm, cupSpan));
}

lv_obj_t* cupLiveLabel = nullptr;
lv_obj_t* cupLiveFill = nullptr;
int cupLastShownMm = -2;

// The one settings screen that genuinely needs a live number: you watch it move as
// you set a glass down. Still compares before writing, so an unchanging reading
// invalidates nothing.
// Says why, not just whether. cupPresent() is true whenever the sensor is disabled
// - that is the deliberate override - so reporting a cup in that state would be a
// lie on the one screen where the owner is diagnosing the thing.
const char* cupStatusText(uint32_t& colour) {
  const auto& sensor = g.config->cupSensor;
  if (g.cupSensorWired && !g.cupSensorWired()) {
    colour = hmi::colour::kTextDim;
    return "NO SENSOR WIRED";
  }
  if (!sensor.enabled) {
    colour = hmi::colour::kUnavailable;
    return "SENSOR OFF";
  }
  if (!hmi::cupThresholdsSane(sensor)) {
    colour = hmi::colour::kUnavailable;
    return "THRESHOLDS OVERLAP";
  }
  const bool cup = g.cupPresent && g.cupPresent();
  colour = cup ? hmi::colour::kAccent : hmi::colour::kTextDim;
  return cup ? "CUP DETECTED" : "NO CUP";
}

void refreshCupSensor() {
  if (cupStatus) {
    uint32_t colour = hmi::colour::kTextDim;
    const char* text = cupStatusText(colour);
    if (strcmp(lv_label_get_text(cupStatus), text) != 0) {
      lv_label_set_text(cupStatus, text);
      lv_obj_set_style_text_color(cupStatus, col(colour), 0);
    }
  }
  if (!cupLiveLabel) return;
  const float mm = g.cupDistanceMm ? g.cupDistanceMm() : -1.0f;
  const int shown = mm < 0.0f ? -1 : (int)(mm + 0.5f);
  if (shown == cupLastShownMm) return;
  cupLastShownMm = shown;

  if (shown < 0) {
    lv_label_set_text(cupLiveLabel, "--");
    lv_obj_set_width(cupLiveFill, 0);
    return;
  }
  char text[16];
  snprintf(text, sizeof(text), "%d mm", shown);
  lv_label_set_text(cupLiveLabel, text);

  const int span = cupSpan;
  int w = (int)((float)shown / kCupScaleMm * (float)span);
  if (w < 0) w = 0;
  if (w > span) w = span;
  lv_obj_set_width(cupLiveFill, w);
}

lv_obj_t* cupEmptyLabel = nullptr;
lv_obj_t* cupCupLabel = nullptr;
lv_obj_t* cupDebounceLabel = nullptr;


lv_obj_t* wifiRead = nullptr;
lv_obj_t* wifiEdit = nullptr;
lv_obj_t* wifiConfirm = nullptr;
lv_obj_t* ssidField = nullptr;
lv_obj_t* passField = nullptr;
lv_obj_t* wifiError = nullptr;

void setStatus(const char* text) {
  if (!holdStatus) return;
  if (std::string(lv_label_get_text(holdStatus)) == text) return;
  lv_label_set_text(holdStatus, text);
}

void stopHold() {
  if (holdChannel < 0) return;
  if (g.setPump) g.setPump(holdChannel, false);
  holdChannel = -1;
  holdUntilMs = 0;
  setStatus("HOLD TO RUN");
}

bool machineBusy() { return g.scheduler && g.scheduler->state() == pour::State::pouring; }

int channelCount() {
  const int pumps = g.config->pumpCount > 0 ? g.config->pumpCount : 8;
  return pumps > kMaxChannels ? kMaxChannels : pumps;
}

const bartender::Slot* slotFor(int index) {
  for (const auto& slot : g.config->slots) {
    if (slot.index == index) return &slot;
  }
  return nullptr;
}

std::string channelName(int channel) {
  const bartender::Slot* slot = slotFor(channel);
  if (!slot || !slot->ingredientId) return "Empty";
  const auto* ingredient = hmi::findIngredient(*g.library, *slot->ingredientId);
  return ingredient ? ingredient->name : *slot->ingredientId;
}

// The rate lives on the bottle now, so an unassigned channel has none.
std::string calIngredientId() {
  const bartender::Slot* slot = slotFor(calChannel);
  return slot && slot->ingredientId ? *slot->ingredientId : std::string();
}

float calRate() { return hmi::calibrationOf(*g.library, calIngredientId()); }

void paintDose() {
  if (doseTrack) lv_slider_set_value(doseTrack, hmi::doseSlider(calRate()), LV_ANIM_OFF);
}

void applyRate(float rate) {
  const std::string id = calIngredientId();
  if (id.empty()) return;
  {
    StateGuard guard;
    hmi::setCalibration(*g.library, id, rate);
  }
  if (g.persistLibrary) g.persistLibrary();
  paintDose();
}

void doseCb(lv_event_t* event) {
  if (machineBusy()) return;
  const hmi::Dose dose = (hmi::Dose)(intptr_t)lv_event_get_user_data(event);
  applyRate(hmi::nudgeMlPerSecond(calRate(), dose));
}

void doseSliderCb(lv_event_t* event) {
  if (machineBusy()) return;
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(event);
  applyRate(hmi::doseSliderRate(lv_slider_get_value(slider)));
}

void startPump(int channel, uint32_t runForMs, const char* word) {
  if (machineBusy() || !g.setPump) {
    setStatus("BUSY POURING");
    return;
  }
  if (holdChannel >= 0 && holdChannel != channel) stopHold();
  holdChannel = channel;
  holdStartMs = lv_tick_get();
  holdSeenMs = holdStartMs;
  holdUntilMs = runForMs ? holdStartMs + runForMs : 0;
  g.setPump(channel, true);
  setStatus(word);
}

// PRIME runs only while held: PRESSING drives the pump, RELEASED and PRESS_LOST
// both stop it, which is the correct interlock for anything that moves liquid.
void holdPumpCb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  const char* word = (const char*)lv_event_get_user_data(event);

  if (code == LV_EVENT_PRESSED) {
    startPump(calChannel, 0, word);
  } else if (code == LV_EVENT_PRESSING) {
    if (holdChannel == calChannel && holdUntilMs == 0) holdSeenMs = lv_tick_get();
  } else if (holdChannel == calChannel && holdUntilMs == 0) {
    stopHold();
  }
}

void testPourCb(lv_event_t*) {
  const float rate = calRate();
  if (rate <= 0.0f) return;
  const uint32_t ms = (uint32_t)(kTestMl / rate * 1000.0f);
  startPump(calChannel, ms, "TESTING 10 ML");
  if (holdChannel < 0) return;
  // pumpHoldTick only runs at 4 Hz, which is a 6 % error on a four-second dose,
  // so the deadline it enforces is the backstop and this one-shot is the timing.
  lv_timer_set_repeat_count(lv_timer_create([](lv_timer_t*) { stopHold(); }, ms, nullptr), 1);
}

lv_obj_t* twoLineButton(lv_obj_t* parent, const char* title, const char* caption,
                        uint32_t titleColour, uint32_t borderColour, int x, int y, int w, int h) {
  lv_obj_t* obj = button(parent, nullptr, ButtonKind::neutral, w, h);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_style_border_color(obj, col(borderColour), 0);
  lv_obj_t* name = label(obj, title, fontTitle(), titleColour);
  lv_obj_align(name, LV_ALIGN_CENTER, 0, -18);
  lv_obj_t* sub = eyebrow(obj, caption, hmi::colour::kTextDim, 1);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sub, w - 32);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22);
  return obj;
}

void holdWiring(lv_obj_t* obj, const char* word) {
  lv_obj_add_event_cb(obj, holdPumpCb, LV_EVENT_PRESSED, (void*)word);
  lv_obj_add_event_cb(obj, holdPumpCb, LV_EVENT_PRESSING, (void*)word);
  lv_obj_add_event_cb(obj, holdPumpCb, LV_EVENT_RELEASED, (void*)word);
  lv_obj_add_event_cb(obj, holdPumpCb, LV_EVENT_PRESS_LOST, (void*)word);
}

// One tuning row: a tracked label on the left, then a -/+ pair around a value on
// the right, all three sized for a finger. The caller owns the value label so it
// can repaint just that one on every step.
lv_obj_t* stepper(lv_obj_t* parent, const char* title, lv_event_cb_t cb, void* down, void* up,
                  const char* initial, int x, int y, int w) {
  const int right = x + w;
  lv_obj_t* cap = eyebrow(parent, title, hmi::colour::kTextDim, 1);
  lv_obj_set_pos(cap, x, y + 16);

  lv_obj_t* minus = button(parent, LV_SYMBOL_MINUS, ButtonKind::neutral, 56, 48);
  lv_obj_set_pos(minus, right - 56 - 8 - 96 - 8 - 56, y);
  lv_obj_add_event_cb(minus, cb, LV_EVENT_CLICKED, down);
  lv_obj_add_event_cb(minus, cb, LV_EVENT_LONG_PRESSED_REPEAT, down);

  lv_obj_t* value = label(parent, initial, fontBody(), hmi::colour::kTextPrimary);
  lv_obj_set_width(value, 96);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(value, right - 56 - 8 - 96, y + 14);

  lv_obj_t* plus = button(parent, LV_SYMBOL_PLUS, ButtonKind::neutral, 56, 48);
  lv_obj_set_pos(plus, right - 56, y);
  lv_obj_add_event_cb(plus, cb, LV_EVENT_CLICKED, up);
  lv_obj_add_event_cb(plus, cb, LV_EVENT_LONG_PRESSED_REPEAT, up);
  return value;
}

void cupNudgeCb(lv_event_t* event) {
  const int packed = (int)(intptr_t)lv_event_get_user_data(event);
  const int field = packed >> 2;
  const int steps = (packed & 1) ? 1 : -1;
  auto& sensor = g.config->cupSensor;

  char text[16];
  if (field == 0) {
    sensor.emptyMm = hmi::nudgeCupMm(sensor.emptyMm, steps);
    snprintf(text, sizeof(text), "%d mm", (int)(sensor.emptyMm + 0.5f));
    if (cupEmptyLabel) lv_label_set_text(cupEmptyLabel, text);
    placeCupMarkers();
  } else if (field == 1) {
    sensor.cupMm = hmi::nudgeCupMm(sensor.cupMm, steps);
    snprintf(text, sizeof(text), "%d mm", (int)(sensor.cupMm + 0.5f));
    if (cupCupLabel) lv_label_set_text(cupCupLabel, text);
    placeCupMarkers();
  } else {
    sensor.debounceSamples = hmi::clampDebounce(sensor.debounceSamples + steps);
    snprintf(text, sizeof(text), "%d", sensor.debounceSamples);
    if (cupDebounceLabel) lv_label_set_text(cupDebounceLabel, text);
  }
  if (g.persistConfig) g.persistConfig();
}

void cupEnableCb(lv_event_t* event) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(event);
  g.config->cupSensor.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
  if (g.persistConfig) g.persistConfig();
}

lv_obj_t* darkSwitch(lv_obj_t* parent, bool on, uint32_t onColour) {
  lv_obj_t* sw = lv_switch_create(parent);
  noClickFocus(sw);
  tameWidget(sw, LV_PART_MAIN);
  tameWidget(sw, LV_PART_INDICATOR);
  tameWidget(sw, LV_PART_KNOB);
  lv_obj_set_size(sw, kSwitchW, kSwitchH);
  lv_obj_set_style_bg_color(sw, col(hmi::colour::kPressed), 0);
  lv_obj_set_style_border_width(sw, kBorderThin, 0);
  lv_obj_set_style_border_color(sw, col(hmi::colour::kLine), 0);
  lv_obj_set_style_bg_color(sw, col(onColour), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, col(on ? hmi::colour::kBase : hmi::colour::kTextDim), LV_PART_KNOB);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  return sw;
}

// A slider whose knob is a plain disc and whose track never changes size with
// state, so dragging it cannot relayout anything.
lv_obj_t* flatSlider(lv_obj_t* parent, int w, int h, int knob) {
  lv_obj_t* slider = lv_slider_create(parent);
  noClickFocus(slider);
  tameWidget(slider, LV_PART_KNOB);
  tameWidget(slider, LV_PART_MAIN);
  lv_obj_set_size(slider, w, h);
  lv_obj_set_style_bg_color(slider, col(hmi::colour::kPressed), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, h / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, col(hmi::colour::kAccent), LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, h / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, col(hmi::colour::kTextPrimary), LV_PART_KNOB);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, (knob - h) / 2, LV_PART_KNOB);
  return slider;
}

void showWifiEditor(bool editing) {
  if (!wifiRead || !wifiEdit) return;
  if (editing) {
    lv_obj_add_flag(wifiRead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifiEdit, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(wifiRead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifiEdit, LV_OBJ_FLAG_HIDDEN);
  }
}

void wifiSaveCb(lv_event_t*) {
  const std::string ssid = lv_textarea_get_text(ssidField);
  const std::string passphrase = lv_textarea_get_text(passField);
  const char* problem = nullptr;
  if (!hmi::wifiSsidValid(ssid)) {
    problem = "A network name must be 1 to 32 characters.";
  } else if (!hmi::wifiPassphraseValid(passphrase)) {
    problem = "A WPA2 passphrase must be 8 to 63 characters.";
  }
  if (problem) {
    lv_label_set_text(wifiError, problem);
    lv_obj_clear_flag(wifiError, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(wifiError, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiConfirm, LV_OBJ_FLAG_HIDDEN);
}

void wifiCommitCb(lv_event_t*) {
  g.config->wifi.ssid = lv_textarea_get_text(ssidField);
  g.config->wifi.passphrase = lv_textarea_get_text(passField);
  if (g.persistConfig) g.persistConfig();
  show(hmi::Screen::settingsWifi);
}

void wifiFocusCb(lv_event_t* event) {
  lv_obj_t* keyboard = (lv_obj_t*)lv_event_get_user_data(event);
  lv_keyboard_set_textarea(keyboard, (lv_obj_t*)lv_event_get_target(event));
}

lv_obj_t* credentialField(lv_obj_t* parent, const char* text, int y, lv_obj_t* keyboard) {
  lv_obj_t* field = lv_textarea_create(parent);
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_text(field, text);
  lv_obj_set_pos(field, 40, y);
  lv_obj_set_size(field, 440, 56);
  lv_obj_set_style_bg_color(field, col(hmi::colour::kRaised), 0);
  lv_obj_set_style_text_color(field, col(hmi::colour::kTextPrimary), 0);
  lv_obj_set_style_border_color(field, col(hmi::colour::kLine), 0);
  lv_obj_set_style_border_color(field, col(hmi::colour::kAccent), LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(field, kBorderThin, 0);
  lv_obj_set_style_radius(field, kRadiusChip, 0);
  lv_obj_set_style_text_font(field, fontTitle(), 0);
  lv_obj_add_event_cb(field, wifiFocusCb, LV_EVENT_CLICKED, keyboard);
  return field;
}

}  // namespace

void pumpHoldTick() {
  if (current != hmi::Screen::settingsCalibration) {
    holdStatus = nullptr;
    stopHold();
    return;
  }
  if (holdChannel < 0) return;
  if (holdUntilMs) {
    if ((int32_t)(lv_tick_get() - holdUntilMs) >= 0) stopHold();
    return;
  }
  if (lv_tick_elaps(holdSeenMs) > kHoldGraceMs || lv_tick_elaps(holdStartMs) > kHoldCeilingMs) {
    stopHold();
  }
}

lv_obj_t* buildCalibration() {
  lv_obj_t* root = screenRoot();
  holdStatus = nullptr;
  stopHold();
  doseTrack = nullptr;

  const int count = channelCount();
  if (calChannel >= count) calChannel = 0;

  lv_obj_t* header = settingsHeader(root, "Pump calibration", nullptr, false);
  holdStatus = eyebrow(header, "HOLD TO RUN", hmi::colour::kTextDim, 1);
  lv_obj_align(holdStatus, LV_ALIGN_RIGHT_MID, -kGutter, 0);

  // The channel picker rebuilds the detail pane on selection, which is cheap
  // because nothing on this screen is an image. The design draws four rows and a
  // "5-8" overflow cell; LVGL scrolls the column instead.
  lv_obj_t* list = container(root);
  lv_obj_set_size(list, 180, kBodyH);
  lv_obj_set_pos(list, kBodyX, kBodyY);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < count; i++) {
    const bartender::Slot* slot = slotFor(i);
    const bool assigned = slot && slot->ingredientId;
    const bool chosen = i == calChannel;

    lv_obj_t* row = card(list, 180, 52);
    markSelected(row, chosen);
    if (!assigned && !chosen) lv_obj_set_style_bg_color(row, col(hmi::colour::kDisabled), 0);

    lv_obj_t* stripe = chip(row, assigned ? hmi::hueFor("", i) : hmi::colour::kPressed, 8, 24);
    lv_obj_align(stripe, LV_ALIGN_LEFT_MID, 16, 0);

    char text[48];
    snprintf(text, sizeof(text), "%d - %s", i + 1, fitText(channelName(i), fontBody(), 92).c_str());
    lv_obj_t* name = label(row, text, fontBody(),
                           chosen ? hmi::colour::kAccent
                                  : (assigned ? hmi::colour::kTextPrimary : hmi::colour::kTextDim));
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 36, 0);

    lv_obj_add_event_cb(
        row,
        [](lv_event_t* event) {
          calChannel = (int)(intptr_t)lv_event_get_user_data(event);
          show(hmi::Screen::settingsCalibration);
        },
        LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  const int paneX = kBodyX + 180 + kGutter;
  const int paneW = kBodyW - 180 - kGutter;
  const bool assigned = !calIngredientId().empty();

  lv_obj_t* rate = panel(root, hmi::colour::kRaised, kRadiusTile);
  lv_obj_set_size(rate, paneW, 156);
  lv_obj_set_pos(rate, paneX, kBodyY);

  lv_obj_t* rateCap = eyebrow(rate, "POUR AMOUNT", hmi::colour::kTextDim, 1);
  lv_obj_align(rateCap, LV_ALIGN_TOP_LEFT, 20, 22);
  lv_obj_t* how = label(rate, assigned ? "RUN A TEST, THEN NUDGE" : "NO BOTTLE IN THIS CHANNEL",
                        fontBody(), hmi::colour::kTextSecondary);
  lv_obj_align(how, LV_ALIGN_TOP_RIGHT, -20, 24);

  // A slider alone cannot land a single step with a thumb and the buttons alone
  // are tedious across the range, so the design carries both. Neither shows a
  // number: what the owner can see is the glass, not the pump's ml/s.
  lv_obj_t* less = button(rate, "LESS", ButtonKind::neutral, 88, 72);
  lv_obj_set_style_text_font(less, fontLabel(), 0);
  lv_obj_set_pos(less, 20, 66);
  lv_obj_add_event_cb(less, doseCb, LV_EVENT_CLICKED, (void*)(intptr_t)hmi::Dose::less);
  lv_obj_add_event_cb(less, doseCb, LV_EVENT_LONG_PRESSED_REPEAT, (void*)(intptr_t)hmi::Dose::less);

  lv_obj_t* more = button(rate, "MORE", ButtonKind::neutral, 88, 72);
  lv_obj_set_style_text_font(more, fontLabel(), 0);
  lv_obj_set_pos(more, paneW - 20 - 88, 66);
  lv_obj_add_event_cb(more, doseCb, LV_EVENT_CLICKED, (void*)(intptr_t)hmi::Dose::more);
  lv_obj_add_event_cb(more, doseCb, LV_EVENT_LONG_PRESSED_REPEAT, (void*)(intptr_t)hmi::Dose::more);

  doseTrack = flatSlider(rate, paneW - 2 * (20 + 88 + 16), 16, 44);
  lv_obj_set_pos(doseTrack, 20 + 88 + 16, 94);
  lv_slider_set_range(doseTrack, hmi::kDoseSliderMin, hmi::kDoseSliderMax);
  lv_obj_add_event_cb(doseTrack, doseSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
  paintDose();

  setEnabled(less, assigned);
  setEnabled(more, assigned);
  setEnabled(doseTrack, assigned);
  if (!assigned) {
    lv_obj_set_style_bg_color(doseTrack, col(hmi::colour::kPressed), LV_PART_INDICATOR);
  }

  const int actionY = kBodyY + 156 + kGutter;
  const int actionH = kBodyH - 156 - kGutter;
  const int actionW = (paneW - kGutter) / 2;

  // One hold-to-run control, not two: PRIME and PURGE differed only in their
  // label and colour, and the pumps cannot run backwards.
  holdWiring(twoLineButton(root, "PRIME", "HOLD TO RUN", hmi::colour::kTextPrimary,
                           hmi::colour::kLine, paneX, actionY, actionW, actionH),
             "PRIMING");

  lv_obj_t* test = card(root, actionW, actionH);
  lv_obj_set_pos(test, paneX + actionW + kGutter, actionY);
  lv_obj_t* testName = label(test, "TEST", fontTitle(), hmi::colour::kTextPrimary);
  lv_obj_align(testName, LV_ALIGN_CENTER, 0, -18);
  lv_obj_t* testUnit = eyebrow(test, "10 ML", hmi::colour::kTextDim, 1);
  lv_obj_align(testUnit, LV_ALIGN_CENTER, 0, 22);
  lv_obj_add_event_cb(test, testPourCb, LV_EVENT_CLICKED, nullptr);
  return root;
}

lv_obj_t* buildCupSensor() {
  lv_obj_t* root = screenRoot();
  const auto& sensor = g.config->cupSensor;
  // The header status is the accent, not green: a status colour that appears
  // once is not a status colour, it is decoration.
  lv_obj_t* header = settingsHeader(root, "Cup sensor", nullptr, false);
  cupStatus = eyebrow(header, "", hmi::colour::kTextDim, 1);
  lv_obj_align(cupStatus, LV_ALIGN_RIGHT_MID, -kGutter, 0);

  const int rightW = 280;
  const int leftW = kBodyW - rightW - kGutter;

  lv_obj_t* enable = panel(root, hmi::colour::kRaised, kRadiusTile);
  lv_obj_set_size(enable, leftW, 96);
  lv_obj_set_pos(enable, kBodyX, kBodyY);
  lv_obj_t* enableName = label(enable, "Sensor enabled", fontBody(), hmi::colour::kTextPrimary);
  lv_obj_align(enableName, LV_ALIGN_TOP_LEFT, 20, 18);
  // The consequence is stated where the decision is made rather than in a
  // confirm dialog after it.
  lv_obj_t* why = label(enable, "Off means pours never pause and never wait for a glass",
                        fontLabel(), hmi::colour::kTextDim);
  lv_label_set_long_mode(why, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(why, leftW - 40 - kSwitchW - 20);
  lv_obj_align(why, LV_ALIGN_TOP_LEFT, 20, 48);
  lv_obj_t* sw = darkSwitch(enable, sensor.enabled, hmi::colour::kAccent);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -20, 0);
  lv_obj_add_event_cb(sw, cupEnableCb, LV_EVENT_VALUE_CHANGED, nullptr);

  const int tuneY = kBodyY + 96 + kGap;
  const int tuneH = kBodyH - 96 - kGap;
  lv_obj_t* tune = panel(root, hmi::colour::kRaised, kRadiusTile);
  lv_obj_set_size(tune, leftW, tuneH);
  lv_obj_set_pos(tune, kBodyX, tuneY);

  lv_obj_t* liveCap = eyebrow(tune, "LIVE READING", hmi::colour::kTextDim, 1);
  lv_obj_align(liveCap, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_t* live = label(tune, "--", fontTitle(), hmi::colour::kTextDim);
  cupLiveLabel = live;
  cupLastShownMm = -2;
  lv_obj_align(live, LV_ALIGN_TOP_RIGHT, -20, 16);

  lv_obj_t* track = panel(tune, hmi::colour::kPressed, 8);
  lv_obj_set_size(track, leftW - 40, 16);
  lv_obj_set_pos(track, 20, 62);
  cupLiveFill = panel(track, hmi::colour::kAccent, 8);
  lv_obj_set_size(cupLiveFill, 0, 16);
  lv_obj_align(cupLiveFill, LV_ALIGN_LEFT_MID, 0, 0);
  cupTrack = track;
  cupSpan = leftW - 40;
  cupTrackX = 20;
  cupEmptyMark = panel(tune, hmi::colour::kTextDim, 0);
  lv_obj_set_size(cupEmptyMark, 4, 32);
  lv_obj_set_y(cupEmptyMark, 54);
  cupThreshMark = panel(tune, hmi::colour::kTextPrimary, 0);
  lv_obj_set_size(cupThreshMark, 4, 32);
  lv_obj_set_y(cupThreshMark, 54);
  placeCupMarkers();

  char text[16];
  const int stepW = leftW - 40;
  snprintf(text, sizeof(text), "%d mm", (int)(sensor.emptyMm + 0.5f));
  cupEmptyLabel = stepper(tune, "EMPTY TRAY", cupNudgeCb, (void*)(intptr_t)0, (void*)(intptr_t)1,
                          text, 20, 100, stepW);
  snprintf(text, sizeof(text), "%d mm", (int)(sensor.cupMm + 0.5f));
  cupCupLabel = stepper(tune, "THRESHOLD", cupNudgeCb, (void*)(intptr_t)4, (void*)(intptr_t)5, text,
                        20, 158, stepW);
  snprintf(text, sizeof(text), "%d", sensor.debounceSamples);
  cupDebounceLabel = stepper(tune, "SAMPLES", cupNudgeCb, (void*)(intptr_t)8, (void*)(intptr_t)9,
                             text, 20, 216, stepW);

  const int rightX = kBodyX + leftW + kGutter;
  lv_obj_t* calibrate =
      twoLineButton(root, "CALIBRATE", "EMPTY TRAY, THEN A GLASS", hmi::colour::kTextPrimary,
                    hmi::colour::kLine, rightX, kBodyY, rightW, kBodyH - kTakeoverH - kGap);
  setEnabled(calibrate, false);

  lv_obj_t* done = button(root, "DONE", ButtonKind::primary, rightW, kTakeoverH);
  lv_obj_set_pos(done, rightX, kBodyY + kBodyH - kTakeoverH);
  onClick(done, hmi::Event::back);
  setRefresh(refreshCupSensor);
  return root;
}

lv_obj_t* buildWifi() {
  lv_obj_t* root = screenRoot();
  settingsHeader(root, "Wi-Fi", "ACCESS POINT UP", false);

  wifiRead = panel(root, hmi::colour::kBase, 0);
  lv_obj_set_size(wifiRead, kScreenW, kScreenH - kHeaderH);
  lv_obj_set_pos(wifiRead, 0, kHeaderH);

  // Both credentials at display size: the brief asked for them to be readable
  // off the panel and typed into a phone from a metre away.
  lv_obj_t* netLabel = eyebrow(wifiRead, "NETWORK", hmi::colour::kTextDim);
  lv_obj_set_pos(netLabel, 40, 36);
  lv_obj_t* ssid =
      label(wifiRead, g.config->wifi.ssid.c_str(), fontDisplay(), hmi::colour::kTextPrimary);
  lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ssid, 720);
  lv_obj_set_pos(ssid, 40, 62);

  lv_obj_t* rule = panel(wifiRead, hmi::colour::kRule, 0);
  lv_obj_set_size(rule, 720, 2);
  lv_obj_set_pos(rule, 40, 124);

  lv_obj_t* passLabel = eyebrow(wifiRead, "PASSWORD", hmi::colour::kTextDim);
  lv_obj_set_pos(passLabel, 40, 144);
  lv_obj_t* pass =
      label(wifiRead, g.config->wifi.passphrase.c_str(), fontDisplay(), hmi::colour::kTextPrimary);
  lv_label_set_long_mode(pass, LV_LABEL_LONG_DOT);
  lv_obj_set_width(pass, 720);
  lv_obj_set_style_text_letter_space(pass, 2, 0);
  lv_obj_set_pos(pass, 40, 170);

  lv_obj_t* hint = label(wifiRead, "Join this network from a phone, then open bartender.local",
                         fontBody(), hmi::colour::kTextDim);
  lv_obj_set_pos(hint, 40, 234);

  // The design's second footer action joined an existing network. The machine
  // only ever runs its own access point, so editing these credentials is the
  // whole screen and the one action spans the full width.
  const int footerY = kScreenH - kHeaderH - 24 - kPrimaryH;
  lv_obj_t* edit = button(wifiRead, "NEW PASSWORD", ButtonKind::neutral, 720, kPrimaryH);
  lv_obj_set_pos(edit, 40, footerY);
  lv_obj_add_event_cb(
      edit, [](lv_event_t*) { showWifiEditor(true); }, LV_EVENT_CLICKED, nullptr);

  wifiEdit = panel(root, hmi::colour::kBase, 0);
  lv_obj_set_size(wifiEdit, kScreenW, kScreenH - kHeaderH);
  lv_obj_set_pos(wifiEdit, 0, kHeaderH);
  lv_obj_add_flag(wifiEdit, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* keyboard = lv_keyboard_create(wifiEdit);
  tameWidget(keyboard, LV_PART_ITEMS);
  lv_obj_set_size(keyboard, kScreenW, 220);
  lv_obj_set_pos(keyboard, 0, kScreenH - kHeaderH - 220);
  lv_obj_set_style_bg_color(keyboard, col(hmi::colour::kBase), 0);
  lv_obj_set_style_border_width(keyboard, 0, 0);
  lv_obj_set_style_pad_all(keyboard, 4, 0);
  lv_obj_set_style_pad_gap(keyboard, 4, 0);
  lv_obj_set_style_bg_color(keyboard, col(hmi::colour::kRaised), LV_PART_ITEMS);
  lv_obj_set_style_text_color(keyboard, col(hmi::colour::kTextPrimary), LV_PART_ITEMS);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard, kRadiusChip, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard, col(hmi::colour::kPressed), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keyboard, col(hmi::colour::kAccent),
                              LV_PART_ITEMS | LV_STATE_PRESSED);

  lv_obj_t* netEdit = eyebrow(wifiEdit, "NETWORK", hmi::colour::kTextDim);
  lv_obj_set_pos(netEdit, 40, 8);
  ssidField = credentialField(wifiEdit, g.config->wifi.ssid.c_str(), 28, keyboard);

  lv_obj_t* passEdit = eyebrow(wifiEdit, "PASSWORD", hmi::colour::kTextDim);
  lv_obj_set_pos(passEdit, 40, 92);
  passField = credentialField(wifiEdit, g.config->wifi.passphrase.c_str(), 112, keyboard);
  lv_keyboard_set_textarea(keyboard, ssidField);

  wifiError = label(wifiEdit, "", fontLabel(), hmi::colour::kDanger);
  lv_obj_set_pos(wifiError, 40, 174);
  lv_obj_add_flag(wifiError, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* save = button(wifiEdit, "SAVE", ButtonKind::primary, 240, kTouchMin);
  lv_obj_set_pos(save, 520, 28);
  lv_obj_add_event_cb(save, wifiSaveCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* cancel = button(wifiEdit, "CANCEL", ButtonKind::neutral, 240, 60);
  lv_obj_set_pos(cancel, 520, 112);
  lv_obj_add_event_cb(
      cancel, [](lv_event_t*) { showWifiEditor(false); }, LV_EVENT_CLICKED, nullptr);

  // Its own click-eating backdrop, so a stray tap cannot reach the keyboard while
  // the warning is up.
  wifiConfirm = panel(root, 0x000000, 0);
  lv_obj_set_size(wifiConfirm, kScreenW, kScreenH);
  lv_obj_set_pos(wifiConfirm, 0, 0);
  lv_obj_set_style_bg_opa(wifiConfirm, 200, 0);
  lv_obj_add_flag(wifiConfirm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(wifiConfirm, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* sheet = panel(wifiConfirm, hmi::colour::kRaised, kRadiusSheet);
  lv_obj_set_size(sheet, 640, 300);
  lv_obj_center(sheet);
  lv_obj_set_style_border_width(sheet, kBorderThin, 0);
  lv_obj_set_style_border_color(sheet, col(hmi::colour::kLine), 0);

  lv_obj_t* heading =
      label(sheet, "This drops the connection", fontTitle(), hmi::colour::kTextPrimary);
  lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 32, 32);
  lv_obj_t* body = label(sheet,
                         "The access point keeps its current name until the machine restarts. "
                         "After that, everything joined to it, including the phone you are "
                         "holding, has to join again on the new one.",
                         fontBody(), hmi::colour::kTextSecondary);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, 576);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 32, 84);

  lv_obj_t* back = button(sheet, "BACK", ButtonKind::neutral, 260, kTouchMin);
  lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 32, -32);
  lv_obj_add_event_cb(
      back, [](lv_event_t*) { lv_obj_add_flag(wifiConfirm, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED,
      nullptr);

  lv_obj_t* commit = button(sheet, "SAVE", ButtonKind::destructive, 260, kTouchMin);
  lv_obj_align(commit, LV_ALIGN_BOTTOM_RIGHT, -32, -32);
  lv_obj_add_event_cb(commit, wifiCommitCb, LV_EVENT_CLICKED, nullptr);
  return root;
}

}  // namespace ui
