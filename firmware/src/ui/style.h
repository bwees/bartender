#pragma once

#include <lvgl.h>

#include <string>

#include "theme.h"

namespace ui {

inline lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

constexpr int kScreenW = 800;
constexpr int kScreenH = 480;
constexpr int kGutter = 16;
constexpr int kGap = 12;
constexpr int kRadiusChip = 6;
constexpr int kRadiusTile = 10;
constexpr int kRadiusSheet = 16;
constexpr int kPrimaryH = 88;
constexpr int kTakeoverH = 96;
constexpr int kTouchMin = 72;

// Border widths never change with state: a pressed style that changes
// LV_STYLE_BORDER_WIDTH moves the object's content box, and every child with it.
constexpr int kBorderThin = 2;
constexpr int kBorderPress = 3;

// The bottom 56 px of the grid is reserved so no tile ever sits under the gear.
constexpr int kGearSize = 56;
constexpr int kGridScrollH = 408;
constexpr int kTileW = 248;
constexpr int kTileH = 198;
constexpr int kTileArtH = 160;
constexpr int kTileBandH = 38;
// How far the artwork is inset from the tile edge, which is the only place a
// tile's pressed fill and border can be seen from under it.
constexpr int kTilePress = 4;

// The detail sheet. Its height leaves a 96 px strip of grid above it, enough for
// the close button to sit clear of the top edge rather than on it.
constexpr int kSheetH = 384;
constexpr int kSheetPad = 24;
constexpr int kSheetArtW = 300;
constexpr int kSheetArtH = 240;

// Every settings screen: 64 px header, 56 px back chevron, 2 px rule.
constexpr int kHeaderH = 64;
constexpr int kBackSize = 56;
constexpr int kSettingsRowW = 376;
constexpr int kSettingsRowH = 118;
constexpr int kSlotCellH = 92;
constexpr int kSwitchW = 76;
constexpr int kSwitchH = 40;

const lv_font_t* fontDisplay();
const lv_font_t* fontTitle();
const lv_font_t* fontBody();
const lv_font_t* fontLabel();
const lv_font_t* fontNumerals();

void stylesInit();

// A bare screen-sized container on the base surface, no scrolling, no padding.
lv_obj_t* screenRoot(uint32_t background = hmi::colour::kBase);

// Every object LVGL creates is click-focusable, and LV_STATE_FOCUSED latches
// until something else is pressed. Nothing here is driven by a keypad or an
// encoder, so focus buys nothing and only leaves widgets looking selected.
void noClickFocus(lv_obj_t* obj);

// For the stock widgets - switch, dropdown, slider, keypad - which arrive
// carrying two pieces of default-theme press feedback we do not want: a
// grow-on-press transform that draws outside the object's own area (the release
// only invalidates the untransformed box, so the grown edge is never painted
// over), and a 35 % dark colour filter that muddies our own pressed colours.
void tameWidget(lv_obj_t* obj, lv_style_selector_t part);

// An invisible, style-less box to lay children out in or scroll.
lv_obj_t* container(lv_obj_t* parent);

// A flat panel: raised fill, tile radius, no border, no scroll, and deaf to
// touch so a decoration laid over a button cannot swallow the press.
lv_obj_t* panel(lv_obj_t* parent, uint32_t fill = hmi::colour::kRaised,
                int radius = kRadiusTile);

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t colour);

// The design's label role is a wide-tracked small cap; LVGL ships no monospace
// face, so tracking carries the whole distinction.
lv_obj_t* eyebrow(lv_obj_t* parent, const char* text, uint32_t colour, int tracking = 2);

enum class ButtonKind { primary, destructive, neutral, quiet };

lv_obj_t* button(lv_obj_t* parent, const char* text, ButtonKind kind, int w, int h);

// A quiet button laid out in its own coordinates. The always-present 3 px border
// moves the content box in, and negative padding of the same size moves it back,
// so a child placed at (20, 0) is 20 px from the drawn edge in every state.
lv_obj_t* card(lv_obj_t* parent, int w, int h);

// The one-of-N look the design uses for concurrency, brightness presets and the
// calibration channel list: pressed fill plus the 3 px accent edge, held.
void markSelected(lv_obj_t* obj, bool selected);

void setEnabled(lv_obj_t* obj, bool enabled);

// The 8 px hue stripe / chip that marks which bottle a row belongs to.
lv_obj_t* chip(lv_obj_t* parent, uint32_t hue, int w, int h);

// lv_dropdown draws its text at its left edge and its chevron at its right, both
// clipped to the object and neither ellipsised, so a name wider than the box
// runs under the chevron. Shortened here instead.
std::string fitText(const std::string& text, const lv_font_t* font, int maxWidth);

}  // namespace ui
