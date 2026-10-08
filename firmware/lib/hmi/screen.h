#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hmi {

enum class Screen {
  attract,
  grid,
  detail,
  awaitingCup,
  pouring,
  paused,
  aborted,
  ready,
  fault,
  pinEntry,
  settings,
  settingsSlots,
  settingsCalibration,
  settingsCupSensor,
  settingsWifi,
  settingsBrightness,
  settingsConcurrency,
};

enum class Event {
  touch,
  idleTimeout,
  selectDrink,
  dismiss,
  pourPressed,
  cancel,
  cupPlaced,
  cupRemoved,
  pourComplete,
  pauseLapsed,
  faultRaised,
  acknowledge,
  makeAnother,
  gear,
  pinAccepted,
  openSlots,
  openCalibration,
  openCupSensor,
  openWifi,
  openBrightness,
  openConcurrency,
  back,
  closeSettings,
};

bool isGuestScreen(Screen screen);
bool isSettingsChild(Screen screen);

// Returns false when the event does not move the machine, leaving next untouched.
bool nextScreen(Screen current, Event event, Screen& next);

// Slow cycle through the drinks that are pourable right now. An empty cycle
// advertises nothing rather than advertising a drink the machine cannot make.
class AttractCycle {
 public:
  void reset(std::vector<int> pourable, uint32_t nowMs);
  bool advance(uint32_t nowMs);
  bool empty() const { return pourable_.empty(); }
  int current() const;
  int size() const { return (int)pourable_.size(); }
  void setDwellMs(uint32_t ms) { dwellMs_ = ms; }

 private:
  std::vector<int> pourable_;
  size_t at_ = 0;
  uint32_t dwellMs_ = 5000;
  uint32_t changedAt_ = 0;
};

enum class PinResult { incomplete, accepted, rejected };

// An empty expected PIN means the gate is not configured and everything is open.
class PinEntry {
 public:
  void clear();
  void backspace();
  PinResult push(char digit, const std::string& expected);
  int length() const { return (int)entered_.size(); }
  bool rejected() const { return rejected_; }

  static int boxCount(const std::string& expected);

 private:
  std::string entered_;
  bool rejected_ = false;
};

}  // namespace hmi
