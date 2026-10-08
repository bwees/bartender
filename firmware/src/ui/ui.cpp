#include "ui.h"

#include <Arduino.h>

#include "../art_cache.h"
#include "../led_ring.h"
#include "../state_lock.h"
#include "ring.h"
#include "ui_internal.h"

namespace ui {

UiState g;
hmi::Screen current = hmi::Screen::attract;
std::vector<hmi::MenuEntry> menu;
int selected = -1;
std::vector<hmi::Leg> legs;
std::vector<float> delivered;
std::string drinkName;
std::string faultCode;
std::string faultHeadline;
std::string faultDetail;
hmi::AttractCycle attract;
hmi::PinEntry pin;

namespace {

Refresh refreshHook = nullptr;
uint32_t knownRevision = 0;
bool lastCup = false;
uint32_t idleLimitMs = 45000;
hmi::Screen pending = hmi::Screen::attract;
bool pendingValid = false;

void readDelivered();

constexpr uint32_t kRingTickMs = 33;

// Reused rather than rebuilt, because this runs 30 times a second.
std::vector<uint32_t> ringFrame;

// What the ring shows is a function of the screen alone — see hmi::ringLook for
// the mapping. Everything here is the paint.
void ringTick() {
  if (!g.config) return;
  const hmi::RingLook look = hmi::ringLook(current);
  const uint32_t now = lv_tick_get();

  const uint8_t effect = look.effect == hmi::RingEffect::pulse
                             ? hmi::pulseLevel(now, look.periodMs)
                             : hmi::kFullLevel;
  const uint8_t level = hmi::ringLevel(effect, g.config->ring.brightness);

  if (look.effect != hmi::RingEffect::rainbow) {
    led::fill(hmi::dim(look.rgb, level));
    return;
  }

  const int pixels = g.config->ring.pixels;
  ringFrame.resize((size_t)(pixels > 0 ? pixels : 0));
  for (int at = 0; at < pixels; at++) {
    ringFrame[at] = hmi::dim(hmi::rainbowPixel(at, pixels, now, look.periodMs), level);
  }
  led::show(ringFrame.data(), pixels);
}

// Drink art is decoded into a widget when the screen is built, so these are the
// screens a new upload has to rebuild rather than tick.
bool showsArt(hmi::Screen screen) {
  return screen == hmi::Screen::attract || screen == hmi::Screen::grid ||
         screen == hmi::Screen::detail;
}

bool tickingScreen(hmi::Screen screen) {
  return screen == hmi::Screen::attract || screen == hmi::Screen::pouring ||
         screen == hmi::Screen::paused || screen == hmi::Screen::settingsCupSensor;
}

void clickCb(lv_event_t* event) {
  dispatch((hmi::Event)(intptr_t)lv_event_get_user_data(event));
}

lv_obj_t* build(hmi::Screen screen) {
  switch (screen) {
    case hmi::Screen::attract: return buildAttract();
    case hmi::Screen::grid: return buildGrid();
    case hmi::Screen::detail: return buildDetail();
    case hmi::Screen::awaitingCup: return buildAwaitingCup();
    case hmi::Screen::pouring: return buildPouring();
    case hmi::Screen::paused: return buildPaused();
    case hmi::Screen::aborted: return buildAborted();
    case hmi::Screen::ready: return buildReady();
    case hmi::Screen::fault: return buildFault();
    case hmi::Screen::pinEntry: return buildPinEntry();
    case hmi::Screen::settings: return buildSettings();
    case hmi::Screen::settingsSlots: return buildSlots();
    case hmi::Screen::settingsCalibration: return buildCalibration();
    case hmi::Screen::settingsCupSensor: return buildCupSensor();
    case hmi::Screen::settingsWifi: return buildWifi();
    case hmi::Screen::settingsConcurrency: return buildConcurrency();
  }
  return screenRoot();
}

void onEnter(hmi::Screen screen) {
  switch (screen) {
    case hmi::Screen::grid:
    case hmi::Screen::attract:
      refreshMenu();
      break;
    case hmi::Screen::awaitingCup: {
      const auto* recipe = selectedRecipe();
      if (recipe) {
        drinkName = recipe->name;
        StateGuard guard;
        legs = hmi::bindLegs(*recipe, *g.config, *g.library);
        delivered.assign(legs.size(), 0.0f);
      }
      break;
    }
    case hmi::Screen::pouring:
      if (g.scheduler->state() == pour::State::paused) {
        g.scheduler->resume();
      } else if (g.scheduler->state() != pour::State::pouring) {
        const auto* recipe = selectedRecipe();
        StateGuard guard;
        if (recipe)
          g.scheduler->begin(pour::buildPlan(*recipe, *g.config, *g.library), g.config->pour);
      }
      break;
    case hmi::Screen::paused:
      g.scheduler->pause();
      break;
    case hmi::Screen::aborted:
      // Read the ledger before aborting, or the last 250 ms of pour goes unreported.
      readDelivered();
      g.scheduler->abort();
      break;
    case hmi::Screen::pinEntry:
      pin.clear();
      break;
    default:
      break;
  }
}

// Screens are rebuilt from inside the event callback of a widget that the
// rebuild destroys, so the swap is deferred to the top of the next
// lv_timer_handler. This also frees the outgoing screen, which lv_scr_load
// on its own does not.
void applyShow(void*) {
  if (!pendingValid) return;
  pendingValid = false;
  lv_obj_t* previous = lv_scr_act();
  refreshHook = nullptr;
  current = pending;
  // Before the outgoing screen's buttons are freed, so a prime or purge cannot
  // outlive the screen that started it.
  pumpHoldTick();
  onEnter(pending);
  lv_scr_load(build(pending));
  if (previous && previous != lv_scr_act()) lv_obj_del(previous);
  // With the outgoing screen gone, the art still held is either bound to the
  // screen just built or bound to nothing.
  art::sweep();
}

void readDelivered() {
  delivered.resize(legs.size());
  for (size_t i = 0; i < legs.size(); i++) {
    float remaining = g.scheduler->remainingMl(legs[i].channel);
    float value = legs[i].plannedMl - remaining;
    delivered[i] = value < 0.0f ? 0.0f : value;
  }
}

void describeFault() {
  switch (g.scheduler->fault()) {
    case pour::Fault::i2c:
      faultCode = "E-02";
      faultHeadline = "The pump driver stopped answering";
      faultDetail =
          "Every channel has been switched off. Check the I2C ribbon to the pump board, then "
          "acknowledge. Nothing will pour until you do.";
      break;
    case pour::Fault::watchdog:
      faultCode = "E-04";
      faultHeadline = "A pump ran past its safety limit";
      faultDetail =
          "A channel was still running well after its calibrated time. Check that line for a kink "
          "or an empty bottle, then acknowledge. Nothing will pour until you do.";
      break;
    default:
      faultCode = "E-01";
      faultHeadline = "The machine stopped mid-pour";
      faultDetail = "All pumps are off. Acknowledge to return to the menu.";
      break;
  }
}

}  // namespace

void show(hmi::Screen screen) {
  pending = screen;
  if (pendingValid) return;
  pendingValid = true;
  lv_async_call(applyShow, nullptr);
}

const bartender::Recipe* selectedRecipe() {
  if (selected < 0 || selected >= (int)menu.size()) return nullptr;
  return &menu[selected].recipe;
}

void refreshMenu() {
  // A portal upload can put new bytes behind a path the cache already holds, and
  // the revision is the only word the HMI gets about it.
  if (stateRevision() != knownRevision) art::flush();
  {
    StateGuard guard;
    menu = hmi::buildMenu(*g.library, g.config->slots);
    knownRevision = stateRevision();
  }
  attract.reset(hmi::pourableIndices(menu), lv_tick_get());
}

void setRefresh(Refresh hook) { refreshHook = hook; }

void onClick(lv_obj_t* obj, hmi::Event event) {
  lv_obj_add_event_cb(obj, clickCb, LV_EVENT_CLICKED, (void*)(intptr_t)event);
}

void dispatch(hmi::Event event) {
  // Acknowledging is what clears the latched fault. Without this poll() sees the
  // scheduler still faulted on its next tick and puts the screen straight back.
  if (event == hmi::Event::acknowledge && current == hmi::Screen::fault && g.scheduler)
    g.scheduler->clearFault();

  hmi::Screen next = current;
  if (!hmi::nextScreen(current, event, next)) return;
  show(next);
}

void poll() {
  if (!g.scheduler) return;

  // A portal edit lands on the HTTP task; this is where the screen notices. It
  // fires on the revision changing, never per tick, and never mid-pour: swapping
  // the menu out from under a running pour is the one time it is unwelcome.
  if (stateRevision() != knownRevision && current != hmi::Screen::pouring) {
    refreshMenu();
    if (selected >= (int)menu.size()) selected = -1;
    if (current == hmi::Screen::detail && selected < 0) {
      show(hmi::Screen::grid);
    } else if (showsArt(current)) {
      // Rebuilt rather than ticked, because art is bound to a widget at build
      // time: attract would otherwise hold the old picture until its next dwell.
      show(current);
    } else if (refreshHook) {
      refreshHook();
    }
  }

  const bool cup = g.cupPresent ? g.cupPresent() : true;

  if (g.scheduler->state() == pour::State::fault && current != hmi::Screen::fault) {
    describeFault();
    dispatch(hmi::Event::faultRaised);
    return;
  }

  switch (current) {
    case hmi::Screen::awaitingCup:
      if (cup) dispatch(hmi::Event::cupPlaced);
      break;
    case hmi::Screen::pouring:
      readDelivered();
      if (!cup && lastCup) {
        dispatch(hmi::Event::cupRemoved);
      } else if (g.scheduler->state() == pour::State::complete) {
        dispatch(hmi::Event::pourComplete);
      }
      break;
    case hmi::Screen::paused:
      if (cup) {
        dispatch(hmi::Event::cupPlaced);
      } else if (g.scheduler->state() == pour::State::aborted) {
        dispatch(hmi::Event::pauseLapsed);
      }
      break;
    case hmi::Screen::ready:
      if (!cup && lastCup) {
        dispatch(hmi::Event::cupRemoved);
      } else if (lv_disp_get_inactive_time(nullptr) > idleLimitMs) {
        dispatch(hmi::Event::idleTimeout);
      }
      break;
    case hmi::Screen::grid:
      if (lv_disp_get_inactive_time(nullptr) > idleLimitMs) dispatch(hmi::Event::idleTimeout);
      break;
    default:
      break;
  }

  // Unconditional: a prime or purge that is still running when the screen goes
  // away has to be stopped by something, and this is the only thing still called.
  pumpHoldTick();

  lastCup = cup;
  // Only the three screens whose content moves on its own get a tick. Everything
  // else redraws when something it shows actually changes, which on a panel this
  // size is the difference between a still image and a full repaint at 4 Hz.
  if (refreshHook && tickingScreen(current)) refreshHook();
}

}  // namespace ui

void uiPoll() { ui::poll(); }

bool uiStart(const UiState& state) {
  if (!state.library || !state.config || !state.scheduler) return false;
  ui::g = state;
  ui::stylesInit();

  ui::refreshMenu();
  ui::show(hmi::Screen::attract);
  lv_timer_handler();

  lv_timer_create([](lv_timer_t*) { uiPoll(); }, 250, nullptr);
  lv_timer_create([](lv_timer_t*) { ui::ringTick(); }, ui::kRingTickMs, nullptr);
  return true;
}
