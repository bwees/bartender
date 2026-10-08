#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../state_lock.h"
#include "ui_internal.h"

namespace ui {
namespace {

// Eight pumps is the hardware, and a board with fewer offers fewer.
constexpr int kConcurrencyMax = 8;

lv_obj_t* pinBoxes[8] = {};
int pinBoxCount = 4;

// Rebuilt with the slots screen and read by its dropdowns' callbacks, which is
// the only thing that maps a chosen row back to an ingredient id.
hmi::SlotChoices slotOptions;
// lv_dropdown_set_text keeps the pointer it is given rather than copying, so the
// shortened names have to outlive the widgets that show them.
std::vector<std::string> slotNames;

const char* kPinMap[] = {"1", "2", "3", "\n", "4", "5", "6", "\n",
                         "7", "8", "9", "\n", "ESC", "0", LV_SYMBOL_BACKSPACE, ""};

void paintPinBoxes() {
  const int filled = pin.length();
  for (int i = 0; i < pinBoxCount; i++) {
    if (!pinBoxes[i]) continue;
    uint32_t border = hmi::colour::kLine;
    if (pin.rejected()) {
      border = hmi::colour::kDanger;
    } else if (i == filled) {
      border = hmi::colour::kAccent;
    }
    lv_obj_set_style_border_color(pinBoxes[i], col(border), 0);
    lv_obj_t* dot = lv_obj_get_child(pinBoxes[i], 0);
    if (dot) {
      if (i < filled) {
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

void pinKeyCb(lv_event_t* event) {
  lv_obj_t* matrix = (lv_obj_t*)lv_event_get_target(event);
  const char* key = lv_btnmatrix_get_btn_text(matrix, lv_btnmatrix_get_selected_btn(matrix));
  if (!key) return;

  if (std::string(key) == "ESC") {
    dispatch(hmi::Event::cancel);
    return;
  }
  if (std::string(key) == LV_SYMBOL_BACKSPACE) {
    pin.backspace();
    paintPinBoxes();
    return;
  }

  const hmi::PinResult result = pin.push(key[0], g.config->pin);
  paintPinBoxes();
  if (result == hmi::PinResult::accepted) dispatch(hmi::Event::pinAccepted);
}

lv_obj_t* settingsGrid(lv_obj_t* root, int top) {
  lv_obj_t* grid = container(root);
  lv_obj_set_size(grid, kScreenW - 2 * kGutter, kScreenH - top);
  lv_obj_set_pos(grid, kGutter, top);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, kGutter, 0);
  return grid;
}

lv_obj_t* settingsRow(lv_obj_t* parent, uint32_t stripe, const char* title, const std::string& value,
                      hmi::Event open) {
  lv_obj_t* row = card(parent, kSettingsRowW, kSettingsRowH);

  lv_obj_t* stripeObj = chip(row, stripe, 8, 32);
  lv_obj_align(stripeObj, LV_ALIGN_LEFT_MID, 20, 0);

  lv_obj_t* name = label(row, title, fontBody(), hmi::colour::kTextPrimary);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 44, -14);
  lv_obj_t* sub = label(row, value.c_str(), fontLabel(), hmi::colour::kTextDim);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sub, kSettingsRowW - 44 - 48);
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 44, 14);

  lv_obj_t* chevron = label(row, LV_SYMBOL_RIGHT, fontTitle(), hmi::colour::kTextDim);
  lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -20, 0);

  onClick(row, open);
  return row;
}

int loadedSlots(int& emptyCount) {
  int loaded = 0;
  emptyCount = 0;
  for (const auto& slot : g.config->slots) {
    if (!slot.ingredientId) continue;
    if (slot.empty) {
      emptyCount++;
    } else {
      loaded++;
    }
  }
  return loaded;
}

const bartender::Slot* slotAt(int index) {
  for (const auto& slot : g.config->slots) {
    if (slot.index == index) return &slot;
  }
  return nullptr;
}

void emptySwitchCb(lv_event_t* event) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(event);
  const int index = (int)(intptr_t)lv_event_get_user_data(event);
  hmi::markSlotEmpty(*g.config, index, lv_obj_has_state(sw, LV_STATE_CHECKED));
  if (g.persistConfig) g.persistConfig();
  refreshMenu();
  show(hmi::Screen::settingsSlots);
}

// A bottle stands in one place, so assigning it here can clear another row. The
// whole screen is rebuilt rather than trying to work out which rows moved.
void slotChoiceCb(lv_event_t* event) {
  lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(event);
  const int index = (int)(intptr_t)lv_event_get_user_data(event);
  const int choice = (int)lv_dropdown_get_selected(dropdown);

  std::optional<std::string> ingredientId;
  if (choice > 0 && choice < (int)slotOptions.ids.size()) ingredientId = slotOptions.ids[choice];
  hmi::assignSlot(*g.config, index, ingredientId);
  if (g.persistConfig) g.persistConfig();
  refreshMenu();
  show(hmi::Screen::settingsSlots);
}

lv_obj_t* ingredientDropdown(lv_obj_t* cell, int index, const bartender::Slot* slot, int width,
                             uint32_t textColour) {
  lv_obj_t* dropdown = lv_dropdown_create(cell);
  noClickFocus(dropdown);
  tameWidget(dropdown, LV_PART_MAIN);
  lv_dropdown_set_options(dropdown, slotOptions.options.c_str());
  lv_dropdown_set_dir(dropdown, LV_DIR_BOTTOM);
  lv_dropdown_set_symbol(dropdown, LV_SYMBOL_DOWN);
  lv_dropdown_set_selected(dropdown,
                           hmi::choiceOf(slotOptions, slot ? slot->ingredientId : std::nullopt));
  // The chevron is drawn hard against the right edge and the name from the left,
  // both clipped to the box and neither ellipsised, so the name is shortened to
  // leave the chevron room rather than running under it.
  slotNames[index] = fitText(slotNames[index], fontBody(), width - 34);
  lv_dropdown_set_text(dropdown, slotNames[index].c_str());

  lv_obj_set_size(dropdown, width, 34);
  lv_obj_set_style_bg_opa(dropdown, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dropdown, 0, 0);
  lv_obj_set_style_pad_all(dropdown, 0, 0);
  lv_obj_set_style_text_font(dropdown, fontBody(), 0);
  lv_obj_set_style_text_color(dropdown, col(textColour), 0);
  lv_obj_set_style_text_color(dropdown, col(hmi::colour::kAccent), LV_STATE_CHECKED);

  // The list is built by the constructor and parented to the screen, so it can be
  // styled now and is never clipped by the cell the row sits in.
  lv_obj_t* list = lv_dropdown_get_list(dropdown);
  lv_obj_set_width(list, 320);
  lv_obj_set_style_max_height(list, 300, 0);
  lv_obj_set_style_bg_color(list, col(hmi::colour::kRaised), 0);
  lv_obj_set_style_text_color(list, col(hmi::colour::kTextPrimary), 0);
  lv_obj_set_style_text_font(list, fontBody(), 0);
  lv_obj_set_style_border_width(list, kBorderThin, 0);
  lv_obj_set_style_border_color(list, col(hmi::colour::kLine), 0);
  lv_obj_set_style_radius(list, kRadiusTile, 0);
  // The list draws its options as one label, so line spacing is what makes a row
  // tall enough to hit with a thumb.
  lv_obj_set_style_text_line_space(list, 26, 0);
  lv_obj_set_style_bg_color(list, col(hmi::colour::kAccentBed), LV_PART_SELECTED);
  lv_obj_set_style_text_color(list, col(hmi::colour::kAccent), LV_PART_SELECTED);

  lv_obj_add_event_cb(dropdown, slotChoiceCb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)index);
  return dropdown;
}

lv_obj_t* emptySwitch(lv_obj_t* cell, int index, bool isEmpty) {
  // Checked means EMPTY, which matches the physical act: you flip it on when the
  // bottle runs out. Amber rather than accent, because this is the one switch in
  // the product whose ON state is bad news.
  lv_obj_t* sw = lv_switch_create(cell);
  noClickFocus(sw);
  tameWidget(sw, LV_PART_MAIN);
  tameWidget(sw, LV_PART_KNOB);
  tameWidget(sw, LV_PART_INDICATOR);
  lv_obj_set_size(sw, kSwitchW, kSwitchH);
  lv_obj_set_style_bg_color(sw, col(hmi::colour::kPressed), 0);
  lv_obj_set_style_border_width(sw, kBorderThin, 0);
  lv_obj_set_style_border_color(sw, col(hmi::colour::kLine), 0);
  lv_obj_set_style_bg_color(sw, col(hmi::colour::kUnavailable),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, col(isEmpty ? hmi::colour::kBase : hmi::colour::kTextDim),
                            LV_PART_KNOB);
  if (isEmpty) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, emptySwitchCb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)index);
  return sw;
}

}  // namespace

lv_obj_t* settingsHeader(lv_obj_t* root, const char* title, const char* status,
                         bool closeInsteadOfBack) {
  lv_obj_t* header = panel(root, hmi::colour::kBase, 0);
  lv_obj_set_size(header, kScreenW, kHeaderH);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(header, kBorderThin, 0);
  lv_obj_set_style_border_color(header, col(hmi::colour::kRule), 0);

  lv_obj_t* back = button(header, closeInsteadOfBack ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT,
                          ButtonKind::quiet, kBackSize, kBackSize);
  lv_obj_set_ext_click_area(back, (88 - kBackSize) / 2);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, kGutter, 0);
  onClick(back, closeInsteadOfBack ? hmi::Event::closeSettings : hmi::Event::back);

  lv_obj_t* name = label(header, title, fontTitle(), hmi::colour::kTextPrimary);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, kGutter + kBackSize + kGutter, 0);

  if (status) {
    lv_obj_t* right = eyebrow(header, status, hmi::colour::kTextDim, 1);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -kGutter, 0);
  }
  return header;
}

lv_obj_t* buildPinEntry() {
  lv_obj_t* root = screenRoot();
  pinBoxCount = hmi::PinEntry::boxCount(g.config->pin);
  if (pinBoxCount > 8) pinBoxCount = 8;

  lv_obj_t* heading = label(root, "Enter PIN", fontTitle(), hmi::colour::kTextPrimary);
  lv_obj_set_pos(heading, 40, 120);

  for (int i = 0; i < pinBoxCount; i++) {
    lv_obj_t* box = panel(root, hmi::colour::kRaised, kRadiusTile);
    lv_obj_set_size(box, 56, 72);
    lv_obj_set_pos(box, 40 + i * 72, 172);
    lv_obj_set_style_border_width(box, kBorderThin, 0);
    // A drawn disc rather than a glyph: U+2022 is not in LVGL's Montserrat.
    lv_obj_t* dot = panel(box, hmi::colour::kTextPrimary, 8);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_center(dot);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    pinBoxes[i] = box;
  }

  lv_obj_t* note = label(root, "Settings are for whoever owns the machine, not the party.",
                         fontBody(), hmi::colour::kTextDim);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, 280);
  lv_obj_set_pos(note, 40, 272);

  // Not lv_keyboard, whose number mode still carries punctuation nobody needs.
  lv_obj_t* keys = lv_btnmatrix_create(root);
  noClickFocus(keys);
  tameWidget(keys, LV_PART_ITEMS);
  lv_btnmatrix_set_map(keys, kPinMap);
  lv_obj_set_size(keys, 400, 408);
  lv_obj_set_pos(keys, kScreenW - 40 - 400, (kScreenH - 408) / 2);
  lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(keys, 0, 0);
  lv_obj_set_style_pad_all(keys, 0, 0);
  lv_obj_set_style_pad_gap(keys, 8, 0);

  lv_obj_set_style_bg_color(keys, col(hmi::colour::kRaised), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keys, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_radius(keys, kRadiusTile, LV_PART_ITEMS);
  lv_obj_set_style_text_color(keys, col(hmi::colour::kTextPrimary), LV_PART_ITEMS);
  lv_obj_set_style_text_font(keys, fontTitle(), LV_PART_ITEMS);
  lv_obj_set_style_border_width(keys, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keys, col(hmi::colour::kPressed), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keys, col(hmi::colour::kAccent), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(keys, kBorderPress, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(keys, col(hmi::colour::kAccent), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_event_cb(keys, pinKeyCb, LV_EVENT_VALUE_CHANGED, nullptr);

  paintPinBoxes();
  return root;
}

lv_obj_t* buildSettings() {
  lv_obj_t* root = screenRoot();
  settingsHeader(root, "Settings", nullptr, true);

  // Two columns of three so nothing scrolls: a scrolling settings root is a small
  // cruelty on a touchscreen.
  lv_obj_t* grid = settingsGrid(root, kHeaderH + kGutter);

  int emptyCount = 0;
  const int loaded = loadedSlots(emptyCount);
  char slotsValue[48];
  snprintf(slotsValue, sizeof(slotsValue), "%d loaded, %d empty", loaded, emptyCount);
  // The amber stripe marks the row that needs attention, in the same amber an
  // unavailable drink uses: the guest sees the symptom, the owner the cause.
  settingsRow(grid, emptyCount > 0 ? hmi::colour::kUnavailable : hmi::colour::kPressed, "Slots",
              slotsValue, hmi::Event::openSlots);

  settingsRow(grid, hmi::colour::kPressed, "Pump calibration", "Prime and test each line",
              hmi::Event::openCalibration);
  settingsRow(grid, hmi::colour::kPressed, "Cup sensor",
              g.config->cupSensor.enabled ? "Enabled" : "Disabled", hmi::Event::openCupSensor);
  settingsRow(grid, hmi::colour::kPressed, "Wi-Fi", g.config->wifi.ssid, hmi::Event::openWifi);


  char concurrency[24];
  snprintf(concurrency, sizeof(concurrency), "%d pump%s", g.config->pour.concurrency,
           g.config->pour.concurrency == 1 ? "" : "s");
  settingsRow(grid, hmi::colour::kPressed, "Pour concurrency", concurrency,
              hmi::Event::openConcurrency);
  return root;
}

lv_obj_t* buildSlots() {
  lv_obj_t* root = screenRoot();
  const int pumps = g.config->pumpCount > 0 ? g.config->pumpCount : 8;
  char status[24];
  snprintf(status, sizeof(status), "%d PUMPS", pumps);
  settingsHeader(root, "Slots", status, false);
  {
    StateGuard guard;
    slotOptions = hmi::slotChoices(*g.library);
  }

  // Eight fixed cells in a 2 x 4 grid, no scrolling: eight pumps is the hardware
  // and the screen should say so. A board with more channels than that scrolls.
  lv_obj_t* grid = settingsGrid(root, kHeaderH + 12);
  if (pumps > 8) lv_obj_set_scroll_dir(grid, LV_DIR_VER);

  slotNames.assign(pumps > 0 ? pumps : 1, std::string());

  for (int i = 0; i < pumps; i++) {
    const bartender::Slot* slot = slotAt(i);
    const bool assigned = slot && slot->ingredientId;
    const bool isEmpty = assigned && slot->empty;
    const auto* ingredient =
        assigned ? hmi::findIngredient(*g.library, *slot->ingredientId) : nullptr;
    slotNames[i] = assigned ? (ingredient ? ingredient->name : *slot->ingredientId) : "Not assigned";

    lv_obj_t* cell = card(grid, kSettingsRowW, kSlotCellH);
    lv_obj_set_style_bg_color(cell, col(isEmpty ? hmi::colour::kDisabled : hmi::colour::kRaised), 0);
    if (!assigned) {
      // The design's only dashed border; LVGL 8 draws solid borders only, so an
      // unassigned cell is outlined at half opacity instead.
      lv_obj_set_style_border_opa(cell, LV_OPA_50, 0);
      lv_obj_set_style_border_color(cell, col(hmi::colour::kLine), 0);
    }

    lv_obj_t* stripe =
        chip(cell, (assigned && !isEmpty) ? hmi::hueFor(ingredient ? ingredient->colour : "", i)
                                          : hmi::colour::kPressed,
             8, 56);
    lv_obj_align(stripe, LV_ALIGN_LEFT_MID, 14, 0);

    char caption[32];
    snprintf(caption, sizeof(caption), isEmpty ? "SLOT %d - EMPTY" : "SLOT %d", i + 1);
    lv_obj_t* cap =
        eyebrow(cell, caption, isEmpty ? hmi::colour::kUnavailable : hmi::colour::kTextDim);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 34, 16);

    const int nameW = kSettingsRowW - 34 - 14 - (assigned ? kSwitchW + 12 : 0);
    lv_obj_t* dropdown =
        ingredientDropdown(cell, i, slot, nameW,
                           assigned && !isEmpty ? hmi::colour::kTextPrimary : hmi::colour::kTextDim);
    lv_obj_align(dropdown, LV_ALIGN_TOP_LEFT, 34, 42);
    // The whole cell is the picker: a 376 x 92 target beats a 240 x 34 one.
    lv_obj_add_event_cb(
        cell, [](lv_event_t* e) { lv_dropdown_open((lv_obj_t*)lv_event_get_user_data(e)); },
        LV_EVENT_CLICKED, dropdown);

    if (!assigned) continue;
    lv_obj_t* sw = emptySwitch(cell, i, isEmpty);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -14, 0);
  }
  return root;
}

lv_obj_t* buildConcurrency() {
  lv_obj_t* root = screenRoot();
  settingsHeader(root, "Pour concurrency", nullptr, false);

  int count = g.config->pumpCount > 0 ? g.config->pumpCount : 8;
  if (count > kConcurrencyMax) count = kConcurrencyMax;

  constexpr int kCols = 4;
  const int rows = (count + kCols - 1) / kCols;
  const int cardW = (720 - (kCols - 1) * kGap) / kCols;
  const int cardH = 132;
  const int bodyH = kScreenH - 64;
  const int top = 64 + (bodyH - rows * cardH - (rows - 1) * kGap) / 2;

  for (int i = 0; i < count; i++) {
    const int value = i + 1;
    const bool chosen = g.config->pour.concurrency == value;
    lv_obj_t* choice = card(root, cardW, cardH);
    lv_obj_set_pos(choice, 40 + (i % kCols) * (cardW + kGap),
                   top + (i / kCols) * (cardH + kGap));
    markSelected(choice, chosen);

    char digit[4];
    snprintf(digit, sizeof(digit), "%d", value);
    lv_obj_t* number = label(choice, digit, fontDisplay(),
                             chosen ? hmi::colour::kAccent : hmi::colour::kTextPrimary);
    lv_obj_center(number);

    lv_obj_add_event_cb(
        choice,
        [](lv_event_t* event) {
          g.config->pour.concurrency = (int)(intptr_t)lv_event_get_user_data(event);
          if (g.persistConfig) g.persistConfig();
          show(hmi::Screen::settingsConcurrency);
        },
        LV_EVENT_CLICKED, (void*)(intptr_t)value);
  }
  return root;
}

}  // namespace ui
