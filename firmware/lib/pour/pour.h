#pragma once

#include <cstdint>
#include <vector>

#include "domain.h"

namespace pour {

uint32_t mlToMs(float ml, float mlPerSecond);

float msToMl(uint32_t ms, float mlPerSecond);

uint32_t watchdogCapMs(uint32_t calibratedMs, float factor, uint32_t hardCapMs);

constexpr uint32_t kWatchdogHardCapMs = 60000;

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t millis() const = 0;
};

class PumpDriver {
 public:
  virtual ~PumpDriver() = default;
  virtual bool set(int channel, bool on) = 0;
  virtual bool allOff() = 0;
};

struct PlanItem {
  int channel;
  uint32_t ms;
  float ml;
};

enum class State { idle, pouring, paused, complete, aborted, fault };

enum class Fault { none, i2c, watchdog };

// The library carries the rates: calibration follows the bottle, so the plan
// cannot be built from the slot table alone.
std::vector<PlanItem> buildPlan(const bartender::Recipe& recipe, const bartender::Config& config,
                                const bartender::Library& library);

class Scheduler {
 public:
  Scheduler(const Clock& clock, PumpDriver& pumps);

  bool begin(const std::vector<PlanItem>& plan, const bartender::PourConfig& config);
  void tick();
  bool pause();
  bool resume();
  void abort();

  // A fault latches until acknowledged: without this the only exit is begin(),
  // so the UI re-raises the fault screen on every poll and cannot be dismissed.
  void clearFault();

  State state() const { return state_; }
  Fault fault() const { return fault_; }
  int runningCount() const;
  float plannedMl() const;
  float deliveredMl() const;
  float fractionComplete() const;
  float remainingMl(int channel) const;
  uint32_t deliveredMs(int channel) const;
  uint32_t watchdogCap(int channel) const;
  uint32_t pauseRemainingSeconds() const;

 private:
  struct ChannelState {
    int channel;
    uint32_t plannedMs;
    float plannedMl;
    uint32_t capMs;
    uint32_t deliveredMs;
    uint32_t accountedAt;
    uint32_t runStartedAt;
    bool running;
    bool finished;
  };

  bool start(ChannelState& channel, uint32_t now);
  bool stop(ChannelState& channel);
  void account(ChannelState& channel, uint32_t now);
  void admit(uint32_t now);
  void enterFault(Fault reason);
  bool allFinished() const;
  const ChannelState* find(int channel) const;

  const Clock& clock_;
  PumpDriver& pumps_;
  bartender::PourConfig config_{};
  std::vector<ChannelState> channels_;
  State state_ = State::idle;
  Fault fault_ = Fault::none;
  uint32_t pausedAt_ = 0;
};

}
