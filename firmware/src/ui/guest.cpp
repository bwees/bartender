#include <cstdio>
#include <vector>

#include "../art_cache.h"
#include "../state_lock.h"
#include "ui_internal.h"

namespace ui {
namespace {

lv_obj_t* pourBar = nullptr;
lv_obj_t* pourSegs[8] = {};
lv_obj_t* pourCards[8] = {};
lv_obj_t* pourCardStates[8] = {};
lv_obj_t* pauseDigits = nullptr;
lv_obj_t* pauseBarFill = nullptr;
lv_obj_t* attractName = nullptr;
lv_obj_t* attractBody = nullptr;
lv_obj_t* attractRoot = nullptr;
lv_obj_t* attractArt = nullptr;

constexpr uint32_t kArtFadeMs = 400;
// The delete has to land after the fade, not with it.
constexpr uint32_t kArtFadeSlackMs = 40;
constexpr lv_opa_t kDimmedArtOpa = 140;

// lv_label_set_text and friends invalidate whether or not the value moved, so
// every tick-driven write is compared against what the widget already holds.
int lastSegWidth[8];
int lastCardState[8];
uint32_t lastPauseSeconds;
int lastPauseWidth;

void resetPourCache() {
  for (int i = 0; i < 8; i++) {
    lastSegWidth[i] = -1;
    lastCardState[i] = -1;
  }
}

void selectTile(lv_event_t* event) {
  selected = (int)(intptr_t)lv_event_get_user_data(event);
  dispatch(hmi::Event::selectDrink);
}

// A frame, not an lv_img: a drink whose art will not decode has to be a visible
// object rather than a hole in the screen.
lv_obj_t* artFrame(lv_obj_t* parent, int w, int h, bool dimmed) {
  lv_obj_t* obj = panel(parent, hmi::colour::kRaised, kRadiusTile);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_border_width(obj, kBorderThin, 0);
  lv_obj_set_style_border_color(obj, col(dimmed ? hmi::colour::kPressed : hmi::colour::kLine), 0);
  return obj;
}

void fillWithPlaceholder(lv_obj_t* frame, char initial) {
  lv_obj_set_flex_flow(frame, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(frame, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(frame, 8, 0);

  char glyph[2] = {initial, 0};
  label(frame, glyph, fontDisplay(), hmi::colour::kLine);
  lv_obj_t* caption = label(frame, "NO ART", fontLabel(), hmi::colour::kTextDim);
  lv_obj_set_style_text_letter_space(caption, 2, 0);
}

void bindArt(lv_obj_t* frame, const lv_img_dsc_t* art, bool dimmed) {
  // The radius belongs to the frame, and art laid inside it is square, so the
  // corners have to be clipped or they paint over the rounding.
  lv_obj_set_style_clip_corner(frame, true, 0);

  lv_obj_t* image = lv_img_create(frame);
  lv_img_set_src(image, art);
  lv_obj_center(image);
  if (!dimmed) return;
  lv_obj_set_style_img_recolor(image, col(hmi::colour::kBase), 0);
  lv_obj_set_style_img_recolor_opa(image, kDimmedArtOpa, 0);
}

void releaseOwnedArt(lv_event_t* event) {
  auto* image = (lv_obj_t*)lv_event_get_target(event);
  art::release((const lv_img_dsc_t*)lv_img_get_src(image));
}

// Attract art is decoded outside the cache and freed with its widget: one
// full-screen buffer per pourable drink would not fit in PSRAM.
lv_obj_t* attractLayer(lv_obj_t* parent, const hmi::MenuEntry& entry) {
  const lv_img_dsc_t* art =
      entry.recipe.image ? art::owned(*entry.recipe.image, kScreenW, kScreenH) : nullptr;
  if (!art) {
    lv_obj_t* frame = artFrame(parent, kScreenW, kScreenH, false);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    fillWithPlaceholder(frame, entry.initial);
    return frame;
  }

  lv_obj_t* image = lv_img_create(parent);
  lv_img_set_src(image, art);
  lv_obj_add_event_cb(image, releaseOwnedArt, LV_EVENT_DELETE, nullptr);
  lv_obj_set_pos(image, 0, 0);
  return image;
}

void refreshAttract() {
  if (!attract.advance(lv_tick_get()) || !attractName) return;
  const int at = attract.current();
  if (at < 0 || at >= (int)menu.size()) return;
  lv_label_set_text(attractName, menu[at].recipe.name.c_str());

  // The incoming art goes underneath and the outgoing one fades off it, which
  // cross-fades at one blend per pixel instead of two.
  lv_obj_t* incoming = attractLayer(attractRoot, menu[at]);
  lv_obj_move_background(incoming);
  if (attractArt) {
    lv_obj_fade_out(attractArt, kArtFadeMs, 0);
    lv_obj_del_delayed(attractArt, kArtFadeMs + kArtFadeSlackMs);
  }
  attractArt = incoming;

  lv_obj_set_style_opa(attractBody, LV_OPA_TRANSP, 0);
  lv_obj_fade_in(attractBody, kArtFadeMs, 0);
}

void refreshPouring() {
  const float total = hmi::totalPlannedMl(legs);
  if (total <= 0.0f) return;

  auto segs = hmi::segments(legs, delivered);
  for (size_t i = 0; i < 8; i++) {
    if (!pourSegs[i]) continue;
    int w = -1;
    if (i < segs.size()) {
      w = (int)(segs[i].fraction * 720.0f + 0.5f);
      if (w < 0) w = 0;
    }
    if (w == lastSegWidth[i]) continue;
    lastSegWidth[i] = w;
    if (w < 0) {
      lv_obj_add_flag(pourSegs[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_width(pourSegs[i], w);
      lv_obj_clear_flag(pourSegs[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  auto deck = hmi::cards(legs, delivered);
  for (size_t i = 0; i < deck.size() && i < 8; i++) {
    if (!pourCards[i]) continue;
    if ((int)deck[i].state == lastCardState[i]) continue;
    lastCardState[i] = (int)deck[i].state;
    const bool done = deck[i].state == hmi::LegState::done;
    lv_obj_set_style_bg_color(pourCards[i],
                              col(done ? hmi::colour::kRaised : hmi::colour::kPressed), 0);
    const char* word = done ? "DONE"
                            : (deck[i].state == hmi::LegState::pouring ? "POURING" : "WAITING");
    lv_label_set_text(pourCardStates[i], word);
    lv_obj_set_style_text_color(pourCardStates[i],
                                col(done ? 0x606060 : deck[i].hue), 0);
  }
}

void refreshPaused() {
  if (!pauseDigits) return;
  const uint32_t left = g.scheduler->pauseRemainingSeconds();
  if (left == lastPauseSeconds) return;
  lastPauseSeconds = left;

  char text[8];
  snprintf(text, sizeof(text), "%u", (unsigned)left);
  lv_label_set_text(pauseDigits, text);

  const float fraction = hmi::pauseBarFraction(left, g.config->pour.pauseWindowSeconds);
  const int width = (int)(fraction * 720.0f + 0.5f);
  if (width == lastPauseWidth) return;
  lastPauseWidth = width;
  lv_obj_set_width(pauseBarFill, width);
}

// ABORT NOW is the only long-press in the product, on the only button that
// throws away someone's drink.
void holdToAbort(lv_event_t* event) {
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(event);
  const lv_event_code_t code = lv_event_get_code(event);
  auto* armed = (uint32_t*)lv_event_get_user_data(event);

  if (code == LV_EVENT_PRESSED) {
    *armed = lv_tick_get();
  } else if (code == LV_EVENT_PRESSING) {
    if (*armed && lv_tick_elaps(*armed) >= 400) {
      *armed = 0;
      dispatch(hmi::Event::cancel);
    }
  } else {
    *armed = 0;
  }
  (void)obj;
}

}  // namespace

lv_obj_t* artTile(lv_obj_t* parent, int w, int h, const hmi::MenuEntry& entry, bool dimmed) {
  lv_obj_t* obj = artFrame(parent, w, h, dimmed);
  const lv_img_dsc_t* art =
      entry.recipe.image ? art::cached(*entry.recipe.image, w - 2 * kBorderThin,
                                       h - 2 * kBorderThin)
                         : nullptr;
  if (art) {
    bindArt(obj, art, dimmed);
  } else {
    fillWithPlaceholder(obj, entry.initial);
  }
  return obj;
}

lv_obj_t* buildAttract() {
  lv_obj_t* root = screenRoot();
  // The most forgiving 800 x 480 button you can build.
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  onClick(root, hmi::Event::touch);

  const int at = attract.current();
  const bool has = at >= 0 && at < (int)menu.size();

  attractRoot = root;
  attractArt = has ? attractLayer(root, menu[at]) : nullptr;

  // A flat scrim at 72 %, not a gradient: a 168 px black ramp is exactly the
  // case that bands on 32 levels.
  lv_obj_t* scrim = panel(root, 0x000000, 0);
  lv_obj_set_size(scrim, kScreenW, 168);
  lv_obj_align(scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(scrim, 184, 0);

  lv_obj_t* rule = panel(root, hmi::colour::kAccent, 0);
  lv_obj_set_size(rule, kScreenW, 2);
  lv_obj_align(rule, LV_ALIGN_BOTTOM_MID, 0, -168);

  attractBody = panel(root, 0x000000, 0);
  lv_obj_set_style_bg_opa(attractBody, LV_OPA_TRANSP, 0);
  lv_obj_set_size(attractBody, kScreenW - 80, 168);
  lv_obj_align(attractBody, LV_ALIGN_BOTTOM_LEFT, 40, 0);
  lv_obj_set_flex_flow(attractBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(attractBody, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(attractBody, 14, 0);

  attractName = label(attractBody, has ? menu[at].recipe.name.c_str() : "Nothing loaded",
                      fontDisplay(), hmi::colour::kTextPrimary);
  lv_obj_t* cta = eyebrow(attractBody, has ? "TAP TO START" : "TAP TO SET UP", hmi::colour::kAccent);
  lv_obj_set_style_text_letter_space(cta, 3, 0);

  setRefresh(refreshAttract);
  return root;
}

lv_obj_t* buildGrid() {
  lv_obj_t* root = screenRoot();

  lv_obj_t* scroller = container(root);
  lv_obj_set_size(scroller, kScreenW - 2 * kGutter, kGridScrollH);
  lv_obj_set_pos(scroller, kGutter, kGutter);
  lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(scroller, kGap, 0);
  lv_obj_set_style_pad_column(scroller, kGap, 0);
  lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(scroller, LV_SCROLL_SNAP_START);
  lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_AUTO);

  for (size_t i = 0; i < menu.size(); i++) {
    const auto& entry = menu[i];
    lv_obj_t* tile = card(scroller, kTileW, kTileH);
    lv_obj_set_style_bg_color(
        tile, col(entry.available ? hmi::colour::kRaised : hmi::colour::kDisabled), 0);
    lv_obj_add_event_cb(tile, selectTile, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    // The artwork is inset so the tile's own fill and pressed border stay visible
    // around it. LVGL draws both in DRAW_MAIN, before the children, so artwork
    // laid edge to edge would paint the press feedback out.
    lv_obj_t* art = artTile(tile, kTileW - 2 * kTilePress, kTileArtH - 2 * kTilePress,
                            entry, !entry.available);
    lv_obj_set_pos(art, kTilePress, kTilePress);
    lv_obj_set_style_radius(art, kRadiusTile, 0);

    if (!entry.available) {
      // The solid amber bar is what reads at 3 m; the dimming is what reads at 0.5 m.
      lv_obj_t* bar = panel(art, hmi::colour::kPressed, 0);
      lv_obj_set_size(bar, kTileW - 2 * kTilePress - 4, 36);
      lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_t* text = label(bar, entry.badge.c_str(), fontLabel(), hmi::colour::kUnavailable);
      lv_obj_align(text, LV_ALIGN_LEFT_MID, 12, 0);
      lv_obj_set_style_text_letter_space(text, 1, 0);
    }

    lv_obj_t* name = label(tile, entry.recipe.name.c_str(), fontBody(),
                           entry.available ? hmi::colour::kTextPrimary : hmi::colour::kTextDim);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, kTileW - 28);
    lv_obj_set_pos(name, 14, kTileArtH + (kTileBandH - 24) / 2);
  }

  // 56 px drawn, 88 px effective: padding cannot widen a hit area, ext_click_area can.
  lv_obj_t* gear = button(root, LV_SYMBOL_SETTINGS, ButtonKind::neutral, kGearSize, kGearSize);
  lv_obj_set_ext_click_area(gear, (88 - kGearSize) / 2);
  lv_obj_set_style_radius(gear, kGearSize / 2, 0);
  lv_obj_align(gear, LV_ALIGN_BOTTOM_RIGHT, -kGutter, 0);
  onClick(gear, hmi::Event::gear);
  return root;
}

lv_obj_t* buildDetail() {
  lv_obj_t* root = buildGrid();

  lv_obj_t* dimmer = panel(root, 0x000000, 0);
  lv_obj_set_size(dimmer, kScreenW, kScreenH);
  lv_obj_set_style_bg_opa(dimmer, 174, 0);
  lv_obj_add_flag(dimmer, LV_OBJ_FLAG_CLICKABLE);

  const auto* recipe = selectedRecipe();
  if (!recipe) return root;
  const auto& entry = menu[selected];

  lv_obj_t* sheet = panel(root, hmi::colour::kRaised, kRadiusSheet);
  lv_obj_set_size(sheet, kScreenW, kSheetH);
  lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);

  // The only entrance animation on a guest screen, and the value is an offset
  // from the bottom alignment, not an absolute y: setting y on an aligned object
  // moves it relative to where the alignment already put it.
  lv_anim_t slide;
  lv_anim_init(&slide);
  lv_anim_set_var(&slide, sheet);
  lv_anim_set_values(&slide, kSheetH, 0);
  lv_anim_set_time(&slide, 180);
  lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&slide, [](void* obj, int32_t y) { lv_obj_set_y((lv_obj_t*)obj, y); });
  lv_anim_start(&slide);
  lv_obj_set_style_border_side(sheet, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(sheet, 2, 0);
  lv_obj_set_style_border_color(sheet, col(hmi::colour::kLine), 0);
  lv_obj_set_style_pad_all(sheet, kSheetPad, 0);

  // Measured, not derived: the sheet's 2 px top rule counts as border on every
  // side for layout, so a content box computed from the padding alone is 4 px
  // too generous and POUR hangs over the bottom edge by exactly that much.
  lv_obj_update_layout(sheet);
  const int contentW = lv_obj_get_content_width(sheet);
  const int contentH = lv_obj_get_content_height(sheet);
  const int colX = kSheetArtW + kSheetPad;
  const int colW = contentW - colX;

  lv_obj_t* art = artTile(sheet, kSheetArtW, kSheetArtH, entry, !entry.available);
  lv_obj_set_pos(art, 0, 0);

  lv_obj_t* title = label(sheet, recipe->name.c_str(), fontDisplay(),
                          entry.available ? hmi::colour::kTextPrimary : hmi::colour::kTextSecondary);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, colW);
  lv_obj_set_pos(title, colX, 0);

  int listY = 56;
  if (!entry.available) {
    lv_obj_t* banner = panel(sheet, hmi::colour::kPressed, kRadiusChip);
    lv_obj_set_size(banner, colW, 44);
    lv_obj_set_pos(banner, colX, listY);
    lv_obj_t* dot = chip(banner, hmi::colour::kUnavailable, 8, 22);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t* why = label(banner, entry.reason.c_str(), fontBody(), hmi::colour::kUnavailable);
    lv_label_set_long_mode(why, LV_LABEL_LONG_DOT);
    lv_obj_set_width(why, colW - 48);
    lv_obj_align(why, LV_ALIGN_LEFT_MID, 34, 0);
    listY += 56;
  }

  // POUR is measured off the bottom first and the ingredient list gets what is
  // left, so a six-pour drink scrolls rather than pushing the button off the
  // sheet. Ticket 010 sizes the button for someone who has had a drink.
  const int pourY = contentH - kPrimaryH;
  lv_obj_t* list = container(sheet);
  lv_obj_set_size(list, colW, pourY - listY - 16);
  lv_obj_set_pos(list, colX, listY);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  std::vector<hmi::SheetRow> rows;
  {
    StateGuard guard;
    rows = hmi::sheetRows(*recipe, *g.library, g.config->slots);
  }
  for (const auto& row : rows) {
    lv_obj_t* line = panel(list, hmi::colour::kRaised, 0);
    lv_obj_set_size(line, colW, 30);

    lv_obj_t* swatch = chip(line, row.hue, 8, 26);
    lv_obj_align(swatch, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* name = label(line, row.name.c_str(), fontBody(),
                           row.present ? hmi::colour::kTextPrimary : hmi::colour::kTextDim);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, colW - 20 - 90);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 20, 0);

    char amount[16];
    snprintf(amount, sizeof(amount), "%d ml", (int)(row.ml + 0.5f));
    lv_obj_t* ml = label(line, amount, fontBody(),
                         row.present ? hmi::colour::kTextSecondary : hmi::colour::kTextDim);
    lv_obj_align(ml, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  lv_obj_t* pourBtn = button(sheet, "POUR", ButtonKind::primary, colW, kPrimaryH);
  lv_obj_set_pos(pourBtn, colX, pourY);
  setEnabled(pourBtn, entry.available);
  if (entry.available) onClick(pourBtn, hmi::Event::pourPressed);

  // Deliberately at the opposite corner of the screen from POUR, and clear of
  // the sheet's top edge rather than sitting on it.
  lv_obj_t* close = button(root, LV_SYMBOL_CLOSE, ButtonKind::neutral, 64, 64);
  lv_obj_set_style_radius(close, 32, 0);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -kGutter, kGutter);
  onClick(close, hmi::Event::dismiss);
  return root;
}

lv_obj_t* buildAwaitingCup() {
  lv_obj_t* root = screenRoot();

  // No root padding: every child below already offsets by the 40 px takeover inset,
  // and padding as well pushed everything to 80 and dropped CANCEL onto the cup mark.
  lv_obj_t* eye = eyebrow(root, hmi::upperCase(drinkName).c_str(), hmi::colour::kAccent);
  lv_obj_set_style_text_letter_space(eye, 3, 0);
  lv_obj_set_pos(eye, 40, 40);

  // Two rectangles, not an animation: a loop here would run for as long as
  // nobody puts a glass down, which can be minutes.
  lv_obj_t* mark = panel(root, hmi::colour::kBase, kRadiusTile);
  lv_obj_set_size(mark, 180, 180);
  lv_obj_set_style_border_width(mark, 4, 0);
  lv_obj_set_style_border_color(mark, col(hmi::colour::kAccent), 0);
  lv_obj_align(mark, LV_ALIGN_LEFT_MID, 40, 0);
  lv_obj_t* fill = panel(mark, hmi::colour::kAccentBed, 8);
  lv_obj_set_size(fill, 100, 120);
  lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t* headline = label(root, "Place a cup\non the tray", fontDisplay(),
                             hmi::colour::kTextPrimary);
  lv_obj_align(headline, LV_ALIGN_LEFT_MID, 264, -24);
  lv_obj_t* sub = label(root, "Pouring starts on its own", fontBody(), hmi::colour::kTextSecondary);
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 264, 48);

  // Neutral, not red: nothing has poured yet, so it is not destructive.
  lv_obj_t* cancel = button(root, "CANCEL", ButtonKind::neutral, 280, kTakeoverH);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 40, -40);
  onClick(cancel, hmi::Event::cancel);
  return root;
}

lv_obj_t* buildPouring() {
  lv_obj_t* root = screenRoot();
  lv_obj_set_style_pad_all(root, 0, 0);

  label(root, drinkName.c_str(), fontDisplay(), hmi::colour::kTextPrimary);
  lv_obj_set_pos(lv_obj_get_child(root, 0), 40, 36);

  pourBar = panel(root, hmi::colour::kTrack, 14);
  lv_obj_set_size(pourBar, 720, 28);
  lv_obj_set_pos(pourBar, 40, 124);
  lv_obj_set_flex_flow(pourBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_clip_corner(pourBar, true, 0);

  for (int i = 0; i < 8; i++) {
    pourSegs[i] = nullptr;
    pourCards[i] = nullptr;
    pourCardStates[i] = nullptr;
  }
  resetPourCache();
  for (size_t i = 0; i < legs.size() && i < 8; i++) {
    pourSegs[i] = panel(pourBar, legs[i].hue, 0);
    lv_obj_set_size(pourSegs[i], 0, 28);
    lv_obj_add_flag(pourSegs[i], LV_OBJ_FLAG_HIDDEN);
  }

  const int count = (int)(legs.size() < 8 ? legs.size() : 8);
  const int cardW = count > 0 ? (720 - kGap * (count - 1)) / count : 720;
  for (int i = 0; i < count; i++) {
    lv_obj_t* card = panel(root, hmi::colour::kPressed, kRadiusTile);
    lv_obj_set_size(card, cardW, 104);
    lv_obj_set_pos(card, 40 + i * (cardW + kGap), 180);
    pourCards[i] = card;

    lv_obj_t* underline = panel(card, legs[i].hue, 0);
    lv_obj_set_size(underline, cardW, 6);
    lv_obj_align(underline, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* name = label(card, legs[i].name.c_str(), fontBody(), hmi::colour::kTextPrimary);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, cardW - 36);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 18, -14);

    pourCardStates[i] = label(card, "WAITING", fontLabel(), legs[i].hue);
    lv_obj_set_style_text_letter_space(pourCardStates[i], 2, 0);
    lv_obj_align(pourCardStates[i], LV_ALIGN_LEFT_MID, 18, 16);
  }

  // Full width because it is the only control on the screen and stopping must
  // never be the hard thing to do.
  lv_obj_t* stop = button(root, "STOP", ButtonKind::destructive, 720, kTakeoverH);
  lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -36);
  onClick(stop, hmi::Event::cancel);

  setRefresh(refreshPouring);
  refreshPouring();
  return root;
}

lv_obj_t* buildPaused() {
  lv_obj_t* root = screenRoot(hmi::colour::kDangerBed);

  lv_obj_t* eye = eyebrow(root, "PAUSED", hmi::colour::kDanger);
  lv_obj_set_style_text_letter_space(eye, 3, 0);
  lv_obj_set_pos(eye, 40, 36);

  lv_obj_t* headline = label(root, "Put the cup back", fontDisplay(), hmi::colour::kTextPrimary);
  lv_obj_set_pos(headline, 40, 150);
  lv_obj_t* sub = label(root, "The pour resumes where it stopped", fontBody(),
                        hmi::colour::kTextSecondary);
  lv_obj_set_pos(sub, 40, 206);

  lv_obj_t* ring = panel(root, 0x401410, 110);
  lv_obj_set_size(ring, 220, 220);
  lv_obj_set_style_border_width(ring, 6, 0);
  lv_obj_set_style_border_color(ring, col(hmi::colour::kDanger), 0);
  lv_obj_align(ring, LV_ALIGN_RIGHT_MID, -40, -30);
  lastPauseSeconds = UINT32_MAX;
  lastPauseWidth = -1;
  pauseDigits = label(ring, "0", fontNumerals(), hmi::colour::kTextPrimary);
  lv_obj_align(pauseDigits, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t* unit = label(ring, "SECONDS", fontLabel(), hmi::colour::kDanger);
  lv_obj_set_style_text_letter_space(unit, 2, 0);
  lv_obj_align(unit, LV_ALIGN_CENTER, 0, 34);

  lv_obj_t* track = panel(root, 0x401410, 12);
  lv_obj_set_size(track, 720, 24);
  lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -144);
  lv_obj_set_style_clip_corner(track, true, 0);
  // The single intentional gradient in the system: lemon to danger across 720 px,
  // roughly 250 px per 565 step, far too gradual to band.
  pauseBarFill = panel(track, hmi::colour::kUnavailable, 12);
  lv_obj_set_size(pauseBarFill, 720, 24);
  lv_obj_set_pos(pauseBarFill, 0, 0);
  lv_obj_set_style_bg_grad_color(pauseBarFill, col(hmi::colour::kDanger), 0);
  lv_obj_set_style_bg_grad_dir(pauseBarFill, LV_GRAD_DIR_HOR, 0);

  lv_obj_t* keep = button(root, "KEEP WAITING", ButtonKind::neutral, 344, kTakeoverH);
  lv_obj_align(keep, LV_ALIGN_BOTTOM_LEFT, 40, -24);

  static uint32_t armed = 0;
  lv_obj_t* abortBtn = button(root, "ABORT NOW", ButtonKind::destructive, 344, kTakeoverH);
  lv_obj_align(abortBtn, LV_ALIGN_BOTTOM_RIGHT, -40, -24);
  lv_obj_add_event_cb(abortBtn, holdToAbort, LV_EVENT_PRESSED, &armed);
  lv_obj_add_event_cb(abortBtn, holdToAbort, LV_EVENT_PRESSING, &armed);
  lv_obj_add_event_cb(abortBtn, holdToAbort, LV_EVENT_RELEASED, &armed);
  lv_obj_add_event_cb(abortBtn, holdToAbort, LV_EVENT_PRESS_LOST, &armed);

  setRefresh(refreshPaused);
  refreshPaused();
  return root;
}

lv_obj_t* buildAborted() {
  lv_obj_t* root = screenRoot();

  lv_obj_t* eye = eyebrow(root, "STOPPED", hmi::colour::kDanger);
  lv_obj_set_style_text_letter_space(eye, 3, 0);
  lv_obj_set_pos(eye, 40, 36);

  lv_obj_t* title = label(root, hmi::abortedTitle(drinkName).c_str(), fontDisplay(),
                          hmi::colour::kTextPrimary);
  lv_obj_set_pos(title, 40, 76);

  auto rows = hmi::ledger(legs, delivered);
  int y = 150;
  for (const auto& row : rows) {
    lv_obj_t* name = label(root, row.name.c_str(), fontBody(), hmi::colour::kTextPrimary);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 220);
    lv_obj_set_pos(name, 40, y);

    lv_obj_t* track = panel(root, hmi::colour::kTrack, 8);
    lv_obj_set_size(track, 340, 16);
    lv_obj_set_pos(track, 276, y + 6);
    lv_obj_t* fill = panel(track, row.hue, 8);
    lv_obj_set_size(fill, (int)(row.fraction * 340.0f + 0.5f), 16);

    lv_obj_t* word = label(root, row.quantity.c_str(), fontBody(), hmi::colour::kTextSecondary);
    lv_obj_set_width(word, 130);
    lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(word, 630, y);
    y += 48;
  }

  // No red on a screen where the bad thing has already finished happening.
  lv_obj_t* ok = button(root, "OK", ButtonKind::primary, 720, kTakeoverH);
  lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -36);
  onClick(ok, hmi::Event::acknowledge);
  return root;
}

lv_obj_t* buildReady() {
  lv_obj_t* root = screenRoot();

  lv_obj_t* rule = panel(root, hmi::colour::kAccent, 0);
  lv_obj_set_size(rule, kScreenW, 8);
  lv_obj_set_pos(rule, 0, 0);

  lv_obj_t* headline = label(root, "Take your drink", fontDisplay(), hmi::colour::kTextPrimary);
  lv_obj_align(headline, LV_ALIGN_LEFT_MID, 40, -34);
  lv_obj_t* name = label(root, drinkName.c_str(), fontTitle(), hmi::colour::kTextSecondary);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 26);

  lv_obj_t* again = button(root, "MAKE ANOTHER", ButtonKind::primary, 360, kTakeoverH);
  lv_obj_align(again, LV_ALIGN_BOTTOM_LEFT, 40, -40);
  onClick(again, hmi::Event::makeAnother);
  return root;
}

lv_obj_t* buildFault() {
  lv_obj_t* root = screenRoot(hmi::colour::kDangerBed);

  lv_obj_t* eye = eyebrow(root, ("FAULT " + faultCode).c_str(), hmi::colour::kDanger);
  lv_obj_set_style_text_letter_space(eye, 3, 0);
  lv_obj_set_pos(eye, 40, 40);

  lv_obj_t* headline = label(root, faultHeadline.c_str(), fontDisplay(), hmi::colour::kTextPrimary);
  lv_label_set_long_mode(headline, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(headline, 660);
  lv_obj_align(headline, LV_ALIGN_LEFT_MID, 40, -60);

  lv_obj_t* detail = label(root, faultDetail.c_str(), fontBody(), hmi::colour::kTextSecondary);
  lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(detail, 660);
  lv_obj_align(detail, LV_ALIGN_LEFT_MID, 40, 30);

  lv_obj_t* ack = button(root, "ACKNOWLEDGE", ButtonKind::primary, 408, kTakeoverH);
  lv_obj_align(ack, LV_ALIGN_BOTTOM_LEFT, 40, -40);
  onClick(ack, hmi::Event::acknowledge);

  lv_obj_t* settings = button(root, "SETTINGS", ButtonKind::neutral, 280, kTakeoverH);
  lv_obj_align(settings, LV_ALIGN_BOTTOM_RIGHT, -40, -40);
  onClick(settings, hmi::Event::gear);
  return root;
}

}  // namespace ui
