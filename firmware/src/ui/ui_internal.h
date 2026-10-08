#pragma once

#include <lvgl.h>

#include <string>
#include <vector>

#include "menu.h"
#include "settings.h"
#include "pourview.h"
#include "screen.h"
#include "style.h"
#include "ui.h"

namespace ui {

extern UiState g;
extern hmi::Screen current;
extern std::vector<hmi::MenuEntry> menu;
extern int selected;
extern std::vector<hmi::Leg> legs;
extern std::vector<float> delivered;
extern std::string drinkName;
extern std::string faultCode;
extern std::string faultHeadline;
extern std::string faultDetail;
extern hmi::AttractCycle attract;
extern hmi::PinEntry pin;

void poll();
void dispatch(hmi::Event event);
void show(hmi::Screen screen);
void refreshMenu();
const bartender::Recipe* selectedRecipe();

// A screen whose content moves on its own installs a refresh hook; ui::poll
// calls it at 4 Hz and it repaints only the widgets whose value has changed.
using Refresh = void (*)();
void setRefresh(Refresh hook);

// Dead-man watchdog for prime and purge, driven from ui::poll. A pump only runs
// while a finger is held on the button, and this is what stops it if the touch
// controller stops reporting the release.
void pumpHoldTick();

lv_obj_t* buildAttract();
lv_obj_t* buildGrid();
lv_obj_t* buildDetail();
lv_obj_t* buildAwaitingCup();
lv_obj_t* buildPouring();
lv_obj_t* buildPaused();
lv_obj_t* buildAborted();
lv_obj_t* buildReady();
lv_obj_t* buildFault();

lv_obj_t* buildPinEntry();
lv_obj_t* buildSettings();
lv_obj_t* buildSlots();
lv_obj_t* buildCalibration();
lv_obj_t* buildCupSensor();
lv_obj_t* buildWifi();
lv_obj_t* buildConcurrency();

// Drink art decoded to fit this box, and the initial-and-NO-ART placeholder when
// the drink has no artwork or the card cannot be read.
lv_obj_t* artTile(lv_obj_t* parent, int w, int h, const hmi::MenuEntry& entry, bool dimmed);

// The 64 px settings header: back chevron, title, optional monospace status.
lv_obj_t* settingsHeader(lv_obj_t* root, const char* title, const char* status,
                         bool closeInsteadOfBack);

void onClick(lv_obj_t* obj, hmi::Event event);

}  // namespace ui
