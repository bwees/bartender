#include "pour.h"

namespace pour {
namespace {

const bartender::Ingredient* ingredientById(const bartender::Library& library,
                                            const std::string& id) {
  for (const auto& ingredient : library.ingredients) {
    if (ingredient.id == id) return &ingredient;
  }
  return nullptr;
}

}  // namespace

uint32_t mlToMs(float ml, float mlPerSecond) {
  if (mlPerSecond <= 0.0f) return 0;
  return (uint32_t)((ml / mlPerSecond) * 1000.0f + 0.5f);
}

float msToMl(uint32_t ms, float mlPerSecond) {
  return ((float)ms / 1000.0f) * mlPerSecond;
}

uint32_t watchdogCapMs(uint32_t calibratedMs, float factor, uint32_t hardCapMs) {
  uint32_t cap = (uint32_t)((float)calibratedMs * factor);
  return cap > hardCapMs ? hardCapMs : cap;
}

std::vector<PlanItem> buildPlan(const bartender::Recipe& recipe,
                                const bartender::Config& config,
                                const bartender::Library& library) {
  std::vector<PlanItem> plan;
  for (const auto& pour : recipe.pours) {
    for (const auto& slot : config.slots) {
      if (!slot.ingredientId || *slot.ingredientId != pour.ingredientId || slot.empty) continue;
      const bartender::Ingredient* ingredient = ingredientById(library, pour.ingredientId);
      if (ingredient) {
        plan.push_back({slot.index, mlToMs(pour.ml, ingredient->mlPerSecond), pour.ml});
      }
      break;
    }
  }
  return plan;
}

Scheduler::Scheduler(const Clock& clock, PumpDriver& pumps) : clock_(clock), pumps_(pumps) {}

bool Scheduler::begin(const std::vector<PlanItem>& plan, const bartender::PourConfig& config) {
  config_ = config;
  channels_.clear();
  fault_ = Fault::none;
  pausedAt_ = 0;
  for (const auto& item : plan) {
    ChannelState channel{};
    channel.channel = item.channel;
    channel.plannedMs = item.ms;
    channel.plannedMl = item.ml;
    channel.capMs = watchdogCapMs(item.ms, config.watchdogFactor, kWatchdogHardCapMs);
    channel.finished = item.ms == 0;
    channels_.push_back(channel);
  }

  state_ = State::pouring;
  if (!pumps_.allOff()) {
    enterFault(Fault::i2c);
    return false;
  }
  admit(clock_.millis());
  if (state_ != State::pouring) return false;
  if (allFinished()) state_ = State::complete;
  return true;
}

void Scheduler::clearFault() {
  if (state_ != State::fault) return;
  pumps_.allOff();
  channels_.clear();
  fault_ = Fault::none;
  state_ = State::idle;
}

void Scheduler::tick() {
  const uint32_t now = clock_.millis();

  if (state_ == State::paused) {
    if (now - pausedAt_ >= (uint32_t)config_.pauseWindowSeconds * 1000u) state_ = State::aborted;
    return;
  }
  if (state_ != State::pouring) return;

  for (auto& channel : channels_) {
    if (!channel.running) continue;
    account(channel, now);
    if (channel.deliveredMs >= channel.plannedMs) {
      channel.deliveredMs = channel.plannedMs;
      if (!stop(channel)) return;
      channel.finished = true;
    } else if (now - channel.runStartedAt >= channel.capMs) {
      stop(channel);
      enterFault(Fault::watchdog);
      return;
    }
  }

  admit(now);
  if (state_ != State::pouring) return;
  if (allFinished()) state_ = State::complete;
}

bool Scheduler::pause() {
  if (state_ != State::pouring) return false;
  const uint32_t now = clock_.millis();
  for (auto& channel : channels_) {
    if (!channel.running) continue;
    account(channel, now);
    if (!stop(channel)) return false;
  }
  state_ = State::paused;
  pausedAt_ = now;
  return true;
}

bool Scheduler::resume() {
  if (state_ != State::paused) return false;
  state_ = State::pouring;
  admit(clock_.millis());
  return state_ == State::pouring;
}

void Scheduler::abort() {
  for (auto& channel : channels_) channel.running = false;
  if (!pumps_.allOff()) {
    enterFault(Fault::i2c);
    return;
  }
  state_ = State::aborted;
}

int Scheduler::runningCount() const {
  int count = 0;
  for (const auto& channel : channels_) {
    if (channel.running) count++;
  }
  return count;
}

float Scheduler::plannedMl() const {
  float total = 0.0f;
  for (const auto& channel : channels_) total += channel.plannedMl;
  return total;
}

float Scheduler::deliveredMl() const {
  float total = 0.0f;
  for (const auto& channel : channels_) {
    if (channel.plannedMs == 0) {
      total += channel.plannedMl;
      continue;
    }
    float fraction = (float)channel.deliveredMs / (float)channel.plannedMs;
    if (fraction > 1.0f) fraction = 1.0f;
    total += channel.plannedMl * fraction;
  }
  return total;
}

float Scheduler::fractionComplete() const {
  const float planned = plannedMl();
  if (planned <= 0.0f) return 1.0f;
  const float fraction = deliveredMl() / planned;
  return fraction > 1.0f ? 1.0f : fraction;
}

float Scheduler::remainingMl(int channel) const {
  const ChannelState* found = find(channel);
  if (found == nullptr || found->plannedMs == 0) return 0.0f;
  float fraction = (float)found->deliveredMs / (float)found->plannedMs;
  if (fraction > 1.0f) fraction = 1.0f;
  return found->plannedMl * (1.0f - fraction);
}

uint32_t Scheduler::deliveredMs(int channel) const {
  const ChannelState* found = find(channel);
  return found == nullptr ? 0 : found->deliveredMs;
}

uint32_t Scheduler::watchdogCap(int channel) const {
  const ChannelState* found = find(channel);
  return found == nullptr ? 0 : found->capMs;
}

uint32_t Scheduler::pauseRemainingSeconds() const {
  if (state_ != State::paused) return 0;
  const uint32_t window = (uint32_t)config_.pauseWindowSeconds * 1000u;
  const uint32_t elapsed = clock_.millis() - pausedAt_;
  if (elapsed >= window) return 0;
  return (window - elapsed + 999u) / 1000u;
}

bool Scheduler::start(ChannelState& channel, uint32_t now) {
  if (!pumps_.set(channel.channel, true)) {
    enterFault(Fault::i2c);
    return false;
  }
  channel.running = true;
  channel.runStartedAt = now;
  channel.accountedAt = now;
  return true;
}

bool Scheduler::stop(ChannelState& channel) {
  channel.running = false;
  if (!pumps_.set(channel.channel, false)) {
    enterFault(Fault::i2c);
    return false;
  }
  return true;
}

void Scheduler::account(ChannelState& channel, uint32_t now) {
  channel.deliveredMs += now - channel.accountedAt;
  channel.accountedAt = now;
}

void Scheduler::admit(uint32_t now) {
  int running = runningCount();
  for (auto& channel : channels_) {
    if (running >= config_.concurrency) return;
    if (channel.running || channel.finished) continue;
    if (!start(channel, now)) return;
    running++;
  }
}

void Scheduler::enterFault(Fault reason) {
  fault_ = reason;
  state_ = State::fault;
  for (auto& channel : channels_) channel.running = false;
  pumps_.allOff();
}

bool Scheduler::allFinished() const {
  for (const auto& channel : channels_) {
    if (!channel.finished) return false;
  }
  return true;
}

const Scheduler::ChannelState* Scheduler::find(int channel) const {
  for (const auto& candidate : channels_) {
    if (candidate.channel == channel) return &candidate;
  }
  return nullptr;
}

}
