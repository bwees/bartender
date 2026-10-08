#pragma once

#include "domain.h"

namespace cup {

// A glass sits above the tray, so a cup always reads *nearer* than the bare tray:
// cupMm < emptyMm. Thresholds the other way round, or too close to separate, are
// unusable rather than merely wrong.
constexpr float kMinSeparationMm = 5.0f;

// The VL53L0X reports 0 for a failed measurement and 8190-odd for no target, so
// readings outside this window are refusals, not distances.
constexpr float kMinRangeMm = 5.0f;
constexpr float kMaxRangeMm = 2000.0f;

// Held either side of the midpoint, as a fraction of the tray-to-cup gap, before a
// committed state will flip.
constexpr float kHysteresisFraction = 0.2f;

enum class Presence { unknown, absent, present };

struct Reading {
  bool valid = false;
  float mm = 0.0f;
};

Reading ranged(float mm);

Reading failed();

bool thresholdsUsable(const bartender::CupSensorConfig& config);

// A sample inside the hysteresis band returns `held` unchanged.
Presence classify(const Reading& sample, const bartender::CupSensorConfig& config, Presence held);

class Detector {
 public:
  Detector() = default;
  explicit Detector(const bartender::CupSensorConfig& config) : config_(config) {}

  // Re-arms only on a config that actually differs, so calling this every poll with
  // the live config costs nothing while a settings edit still restarts the debounce.
  void configure(const bartender::CupSensorConfig& config);

  Presence update(const Reading& sample);

  Presence presence() const { return presence_; }

  // Ticket 006 fails closed: anything short of a confirmed cup refuses the pour, so
  // a silent sensor never pours blind. `enabled == false` is the deliberate override
  // for a dead sensor and reports a cup unconditionally - pours then never wait for
  // a glass and never pause when one is lifted.
  bool cupPresent() const;

 private:
  int samplesRequired() const;

  bartender::CupSensorConfig config_{};
  Presence presence_ = Presence::unknown;
  Presence candidate_ = Presence::unknown;
  int agreeing_ = 0;
};

}  // namespace cup
