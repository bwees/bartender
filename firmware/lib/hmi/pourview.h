#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain.h"

namespace hmi {

// One channel of a pour, resolved to the things a screen needs to draw it.
struct Leg {
  int channel = 0;
  std::string name;
  uint32_t hue = 0;
  float plannedMl = 0.0f;
};

enum class LegState { waiting, pouring, done };

struct Segment {
  uint32_t hue = 0;
  float fraction = 0.0f;
};

struct Card {
  std::string name;
  uint32_t hue = 0;
  LegState state = LegState::waiting;
};

struct LedgerRow {
  std::string name;
  uint32_t hue = 0;
  float fraction = 0.0f;
  std::string quantity;
};

// Mirrors pour::buildPlan's ingredient-to-channel mapping, including its skip of
// a pour whose ingredient is not in the library, so the screen and the scheduler
// can never disagree about which pump is pouring what.
std::vector<Leg> bindLegs(const bartender::Recipe& recipe, const bartender::Config& config,
                          const bartender::Library& library);

float totalPlannedMl(const std::vector<Leg>& legs);

LegState legState(float deliveredMl, float plannedMl);

// One bar, segmented by ingredient hue: how far along the drink is and what is
// going into it, in the same 28 px strip.
std::vector<Segment> segments(const std::vector<Leg>& legs,
                              const std::vector<float>& deliveredMl);

std::vector<Card> cards(const std::vector<Leg>& legs, const std::vector<float>& deliveredMl);

// all of it / most / about half / a splash, bucketed at 95 / 60 / 25 %.
std::string quantityWord(float fraction);

std::vector<LedgerRow> ledger(const std::vector<Leg>& legs,
                              const std::vector<float>& deliveredMl);

std::string abortedTitle(const std::string& drinkName);

// The draining bar under the pause countdown.
float pauseBarFraction(uint32_t remainingSeconds, int windowSeconds);

}  // namespace hmi
