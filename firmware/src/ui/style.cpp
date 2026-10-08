#include "style.h"

namespace ui {
namespace {

lv_style_t sBtnPrimary, sBtnPrimaryPressed, sBtnDisabled;
lv_style_t sBtnDestructive, sBtnDestructivePressed;
lv_style_t sBtnNeutral, sBtnNeutralPressed;
lv_style_t sBtnQuiet, sBtnQuietPressed;
lv_style_t sFadeOut, sSnapIn;
bool ready = false;

// PRESSED must register on the same frame at 39 Hz, so the transition into it is
// 0 ms; the 120 ms out is what stops a fast tap from looking like a dropped one.
// lv_obj_set_state collects transitions from every style valid for the state it
// is entering, and a state-0 style is valid in every state, so the 0 ms one has
// to be added last: lv_obj_add_style inserts at the head, and the first match
// with a state weight at least as high wins.
const lv_style_prop_t kTransitionProps[] = {LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR,
                                            LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA,
                                            LV_STYLE_PROP_INV};
lv_style_transition_dsc_t fadeOut, snapIn;

// Border width is deliberately the same in both states of every pair. It is a
// layout property: changing it with state moves the content box and relayouts
// every child, which is what made a pressed tile shove its artwork sideways.
void baseButton(lv_style_t& style, uint32_t fill, uint32_t text, uint32_t border,
                int borderWidth, lv_opa_t borderOpa) {
  lv_style_init(&style);
  lv_style_set_radius(&style, kRadiusTile);
  lv_style_set_bg_opa(&style, LV_OPA_COVER);
  lv_style_set_bg_color(&style, col(fill));
  lv_style_set_text_color(&style, col(text));
  lv_style_set_border_width(&style, borderWidth);
  lv_style_set_border_color(&style, col(border));
  lv_style_set_border_opa(&style, borderOpa);
  lv_style_set_shadow_width(&style, 0);
  lv_style_set_text_font(&style, fontTitle());
}

}  // namespace

const lv_font_t* fontDisplay() { return &lv_font_montserrat_40; }
const lv_font_t* fontTitle() { return &lv_font_montserrat_28; }
const lv_font_t* fontBody() { return &lv_font_montserrat_20; }
const lv_font_t* fontLabel() { return &lv_font_montserrat_16; }
const lv_font_t* fontNumerals() { return &lv_font_montserrat_48; }

void stylesInit() {
  if (ready) return;
  ready = true;

  lv_style_transition_dsc_init(&fadeOut, kTransitionProps, lv_anim_path_linear, 120, 0, nullptr);
  lv_style_init(&sFadeOut);
  lv_style_set_transition(&sFadeOut, &fadeOut);
  lv_style_transition_dsc_init(&snapIn, kTransitionProps, lv_anim_path_linear, 0, 0, nullptr);
  lv_style_init(&sSnapIn);
  lv_style_set_transition(&sSnapIn, &snapIn);

  baseButton(sBtnPrimary, hmi::colour::kAccent, hmi::colour::kBase, hmi::colour::kAccent,
             kBorderThin, LV_OPA_TRANSP);
  baseButton(sBtnPrimaryPressed, hmi::colour::kAccentBed, hmi::colour::kAccent,
             hmi::colour::kAccent, kBorderThin, LV_OPA_COVER);
  // No border width and no radius of its own: those come from whichever kind
  // style sits under it, so switching to DISABLED never changes the geometry.
  lv_style_init(&sBtnDisabled);
  lv_style_set_bg_opa(&sBtnDisabled, LV_OPA_COVER);
  lv_style_set_bg_color(&sBtnDisabled, col(hmi::colour::kRaised));
  lv_style_set_text_color(&sBtnDisabled, col(hmi::colour::kTextDim));
  lv_style_set_border_color(&sBtnDisabled, col(hmi::colour::kPressed));
  lv_style_set_border_opa(&sBtnDisabled, LV_OPA_COVER);

  baseButton(sBtnDestructive, hmi::colour::kBase, hmi::colour::kDanger, hmi::colour::kDanger,
             kBorderThin, LV_OPA_COVER);
  baseButton(sBtnDestructivePressed, hmi::colour::kDanger, hmi::colour::kBase,
             hmi::colour::kDanger, kBorderThin, LV_OPA_COVER);

  baseButton(sBtnNeutral, hmi::colour::kBase, hmi::colour::kTextSecondary, hmi::colour::kLine,
             kBorderThin, LV_OPA_COVER);
  baseButton(sBtnNeutralPressed, hmi::colour::kPressed, hmi::colour::kTextPrimary,
             hmi::colour::kAccent, kBorderThin, LV_OPA_COVER);

  baseButton(sBtnQuiet, hmi::colour::kRaised, hmi::colour::kTextPrimary, hmi::colour::kAccent,
             kBorderPress, LV_OPA_TRANSP);
  baseButton(sBtnQuietPressed, hmi::colour::kPressed, hmi::colour::kTextPrimary,
             hmi::colour::kAccent, kBorderPress, LV_OPA_COVER);
  lv_style_set_text_font(&sBtnQuiet, fontBody());
  lv_style_set_text_font(&sBtnQuietPressed, fontBody());
}

lv_obj_t* screenRoot(uint32_t background) {
  lv_obj_t* root = lv_obj_create(nullptr);
  lv_obj_set_size(root, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(root, col(background), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_radius(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_text_color(root, col(hmi::colour::kTextPrimary), 0);
  lv_obj_set_style_text_font(root, fontBody(), 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  noClickFocus(root);
  return root;
}

void noClickFocus(lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE); }

void tameWidget(lv_obj_t* obj, lv_style_selector_t part) {
  lv_obj_set_style_transform_width(obj, 0, part | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(obj, 0, part | LV_STATE_PRESSED);
  lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, part | LV_STATE_PRESSED);
}

// Stays clickable: a scroll gesture that starts on the bare gaps between tiles
// has to land on something before LVGL will look for a scrollable parent.
lv_obj_t* container(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  noClickFocus(obj);
  return obj;
}

lv_obj_t* panel(lv_obj_t* parent, uint32_t fill, int radius) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_style_bg_color(obj, col(fill), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  noClickFocus(obj);
  return obj;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t colour) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, col(colour), 0);
  return obj;
}

lv_obj_t* eyebrow(lv_obj_t* parent, const char* text, uint32_t colour, int tracking) {
  lv_obj_t* obj = label(parent, text, fontLabel(), colour);
  lv_obj_set_style_text_letter_space(obj, tracking, 0);
  return obj;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, ButtonKind kind, int w, int h) {
  stylesInit();
  lv_obj_t* obj = lv_btn_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, w, h);
  lv_obj_add_style(obj, &sFadeOut, 0);
  noClickFocus(obj);

  switch (kind) {
    case ButtonKind::primary:
      lv_obj_add_style(obj, &sBtnPrimary, 0);
      lv_obj_add_style(obj, &sBtnPrimaryPressed, LV_STATE_PRESSED);
      break;
    case ButtonKind::destructive:
      lv_obj_add_style(obj, &sBtnDestructive, 0);
      lv_obj_add_style(obj, &sBtnDestructivePressed, LV_STATE_PRESSED);
      break;
    case ButtonKind::neutral:
      lv_obj_add_style(obj, &sBtnNeutral, 0);
      lv_obj_add_style(obj, &sBtnNeutralPressed, LV_STATE_PRESSED);
      break;
    case ButtonKind::quiet:
      lv_obj_add_style(obj, &sBtnQuiet, 0);
      lv_obj_add_style(obj, &sBtnQuietPressed, LV_STATE_PRESSED);
      break;
  }
  lv_obj_add_style(obj, &sBtnDisabled, LV_STATE_DISABLED);
  lv_obj_add_style(obj, &sSnapIn, LV_STATE_PRESSED);

  if (text) {
    lv_obj_t* text_obj = lv_label_create(obj);
    lv_label_set_text(text_obj, text);
    lv_obj_center(text_obj);
  }
  return obj;
}

lv_obj_t* card(lv_obj_t* parent, int w, int h) {
  lv_obj_t* obj = button(parent, nullptr, ButtonKind::quiet, w, h);
  lv_obj_set_style_pad_all(obj, -kBorderPress, 0);
  return obj;
}

void markSelected(lv_obj_t* obj, bool selected) {
  lv_obj_set_style_bg_color(obj, col(selected ? hmi::colour::kPressed : hmi::colour::kRaised), 0);
  lv_obj_set_style_border_opa(obj, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

// DISABLED always clears CLICKABLE so a drunk hand gets no response at all,
// rather than a response that does nothing.
void setEnabled(lv_obj_t* obj, bool enabled) {
  if (enabled) {
    lv_obj_clear_state(obj, LV_STATE_DISABLED);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_add_state(obj, LV_STATE_DISABLED);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  }
}

lv_obj_t* chip(lv_obj_t* parent, uint32_t hue, int w, int h) {
  lv_obj_t* obj = panel(parent, hue, w / 2);
  lv_obj_set_size(obj, w, h);
  return obj;
}

std::string fitText(const std::string& text, const lv_font_t* font, int maxWidth) {
  if (lv_txt_get_width(text.c_str(), text.size(), font, 0, LV_TEXT_FLAG_NONE) <= maxWidth) {
    return text;
  }
  std::string out = text;
  while (!out.empty()) {
    out.pop_back();
    const std::string trial = out + "...";
    if (lv_txt_get_width(trial.c_str(), trial.size(), font, 0, LV_TEXT_FLAG_NONE) <= maxWidth) {
      return trial;
    }
  }
  return "...";
}

}  // namespace ui
