#include "cup.h"

#include <cmath>

namespace cup {
namespace {

bool inRange(float mm) { return std::isfinite(mm) && mm >= kMinRangeMm && mm <= kMaxRangeMm; }

bool same(const bartender::CupSensorConfig& a, const bartender::CupSensorConfig& b) {
  return a.enabled == b.enabled && a.emptyMm == b.emptyMm && a.cupMm == b.cupMm &&
         a.debounceSamples == b.debounceSamples;
}

}  // namespace

Reading ranged(float mm) { return {true, mm}; }

Reading failed() { return {false, 0.0f}; }

bool thresholdsUsable(const bartender::CupSensorConfig& config) {
  if (!inRange(config.cupMm) || !inRange(config.emptyMm)) return false;
  return config.cupMm + kMinSeparationMm <= config.emptyMm;
}

Presence classify(const Reading& sample, const bartender::CupSensorConfig& config, Presence held) {
  if (!thresholdsUsable(config)) return Presence::unknown;
  if (!sample.valid || !inRange(sample.mm)) return Presence::unknown;

  const float midpoint = (config.cupMm + config.emptyMm) / 2.0f;
  const float margin = (config.emptyMm - config.cupMm) * kHysteresisFraction;
  if (sample.mm <= midpoint - margin) return Presence::present;
  if (sample.mm >= midpoint + margin) return Presence::absent;
  return held;
}

int Detector::samplesRequired() const {
  return config_.debounceSamples < 1 ? 1 : config_.debounceSamples;
}

void Detector::configure(const bartender::CupSensorConfig& config) {
  if (same(config_, config)) return;
  config_ = config;
  presence_ = Presence::unknown;
  candidate_ = Presence::unknown;
  agreeing_ = 0;
}

Presence Detector::update(const Reading& sample) {
  const Presence candidate = classify(sample, config_, presence_);
  if (candidate == candidate_) {
    if (agreeing_ < samplesRequired()) agreeing_++;
  } else {
    candidate_ = candidate;
    agreeing_ = 1;
  }
  if (candidate_ != presence_ && agreeing_ >= samplesRequired()) presence_ = candidate_;
  return presence_;
}

bool Detector::cupPresent() const {
  if (!config_.enabled) return true;
  return presence_ == Presence::present;
}

}  // namespace cup
