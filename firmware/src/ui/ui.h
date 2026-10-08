#pragma once

#include <functional>

#include "domain.h"
#include "pour.h"
#include "storage.h"

struct UiState {
  bartender::Library* library = nullptr;
  bartender::Config* config = nullptr;
  pour::Scheduler* scheduler = nullptr;
  bartender::Filesystem* fs = nullptr;
  // Absent means no ToF is wired yet, which reads as "a cup is always there" so
  // the machine stays usable. Ticket 006's fail-closed rule is the cup sensor's
  // own concern once it exists.
  std::function<bool()> cupPresent;
  // Raw ToF distance in mm for the cup sensor screen's live bar. Negative when the
  // last read failed.
  std::function<float()> cupDistanceMm;
  // False means nothing answers at 0x29, so the screen can say so rather than quote
  // a distance nothing measured.
  std::function<bool()> cupSensorWired;
  std::function<void(int channel, bool on)> setPump;
  std::function<void()> persistConfig;
  std::function<void()> persistLibrary;
};

bool uiStart(const UiState& state);

// Runs the scheduler, derives events from it and repaints. Call from the LVGL
// task only: it is the one place that touches I2C outside the pump driver.
void uiPoll();
