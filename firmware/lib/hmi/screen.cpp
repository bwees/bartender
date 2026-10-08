#include "screen.h"

namespace hmi {

bool isGuestScreen(Screen screen) {
  switch (screen) {
    case Screen::attract:
    case Screen::grid:
    case Screen::detail:
    case Screen::awaitingCup:
    case Screen::pouring:
    case Screen::paused:
    case Screen::aborted:
    case Screen::ready:
    case Screen::fault:
      return true;
    default:
      return false;
  }
}

bool isSettingsChild(Screen screen) {
  switch (screen) {
    case Screen::settingsSlots:
    case Screen::settingsCalibration:
    case Screen::settingsCupSensor:
    case Screen::settingsWifi:
    case Screen::settingsBrightness:
    case Screen::settingsConcurrency:
      return true;
    default:
      return false;
  }
}

bool nextScreen(Screen current, Event event, Screen& next) {
  if (event == Event::faultRaised) {
    if (current == Screen::fault) return false;
    next = Screen::fault;
    return true;
  }

  if (isSettingsChild(current)) {
    if (event == Event::back) {
      next = Screen::settings;
      return true;
    }
    return false;
  }

  switch (current) {
    case Screen::attract:
      if (event == Event::touch) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::grid:
      if (event == Event::selectDrink) {
        next = Screen::detail;
        return true;
      }
      if (event == Event::idleTimeout) {
        next = Screen::attract;
        return true;
      }
      if (event == Event::gear) {
        next = Screen::pinEntry;
        return true;
      }
      return false;

    case Screen::detail:
      if (event == Event::pourPressed) {
        next = Screen::awaitingCup;
        return true;
      }
      if (event == Event::dismiss) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::awaitingCup:
      if (event == Event::cupPlaced) {
        next = Screen::pouring;
        return true;
      }
      if (event == Event::cancel || event == Event::idleTimeout) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::pouring:
      if (event == Event::pourComplete) {
        next = Screen::ready;
        return true;
      }
      if (event == Event::cupRemoved) {
        next = Screen::paused;
        return true;
      }
      if (event == Event::cancel) {
        next = Screen::aborted;
        return true;
      }
      return false;

    case Screen::paused:
      if (event == Event::cupPlaced) {
        next = Screen::pouring;
        return true;
      }
      if (event == Event::pauseLapsed || event == Event::cancel) {
        next = Screen::aborted;
        return true;
      }
      return false;

    case Screen::aborted:
      if (event == Event::acknowledge) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::ready:
      if (event == Event::cupRemoved || event == Event::idleTimeout) {
        next = Screen::attract;
        return true;
      }
      if (event == Event::makeAnother) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::fault:
      if (event == Event::acknowledge) {
        next = Screen::grid;
        return true;
      }
      // A stalled pump is not the moment to make someone remember four digits.
      if (event == Event::gear) {
        next = Screen::settings;
        return true;
      }
      return false;

    case Screen::pinEntry:
      if (event == Event::pinAccepted) {
        next = Screen::settings;
        return true;
      }
      if (event == Event::cancel) {
        next = Screen::grid;
        return true;
      }
      return false;

    case Screen::settings:
      if (event == Event::closeSettings) {
        next = Screen::grid;
        return true;
      }
      switch (event) {
        case Event::openSlots:
          next = Screen::settingsSlots;
          return true;
        case Event::openCalibration:
          next = Screen::settingsCalibration;
          return true;
        case Event::openCupSensor:
          next = Screen::settingsCupSensor;
          return true;
        case Event::openWifi:
          next = Screen::settingsWifi;
          return true;
        case Event::openBrightness:
          next = Screen::settingsBrightness;
          return true;
        case Event::openConcurrency:
          next = Screen::settingsConcurrency;
          return true;
        default:
          return false;
      }

    default:
      return false;
  }
}

void AttractCycle::reset(std::vector<int> pourable, uint32_t nowMs) {
  pourable_ = std::move(pourable);
  at_ = 0;
  changedAt_ = nowMs;
}

bool AttractCycle::advance(uint32_t nowMs) {
  if (pourable_.size() < 2) return false;
  if (nowMs - changedAt_ < dwellMs_) return false;
  at_ = (at_ + 1) % pourable_.size();
  changedAt_ = nowMs;
  return true;
}

int AttractCycle::current() const {
  if (pourable_.empty()) return -1;
  return pourable_[at_];
}

int PinEntry::boxCount(const std::string& expected) {
  return expected.empty() ? 4 : (int)expected.size();
}

void PinEntry::clear() {
  entered_.clear();
  rejected_ = false;
}

void PinEntry::backspace() {
  rejected_ = false;
  if (!entered_.empty()) entered_.pop_back();
}

PinResult PinEntry::push(char digit, const std::string& expected) {
  if (expected.empty()) return PinResult::accepted;
  if (digit < '0' || digit > '9') return PinResult::incomplete;
  if (rejected_) clear();
  if ((int)entered_.size() >= (int)expected.size()) return PinResult::incomplete;

  entered_.push_back(digit);
  if (entered_.size() < expected.size()) return PinResult::incomplete;

  if (entered_ == expected) {
    clear();
    return PinResult::accepted;
  }
  rejected_ = true;
  return PinResult::rejected;
}

}  // namespace hmi
