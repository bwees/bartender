#include "pourview.h"

#include "menu.h"
#include "theme.h"

namespace hmi {
namespace {

float deliveredAt(const std::vector<float>& delivered, size_t i) {
  return i < delivered.size() ? delivered[i] : 0.0f;
}

}  // namespace

std::vector<Leg> bindLegs(const bartender::Recipe& recipe, const bartender::Config& config,
                          const bartender::Library& library) {
  std::vector<Leg> legs;
  for (const auto& item : recipe.pours) {
    for (const auto& slot : config.slots) {
      if (!slot.ingredientId || *slot.ingredientId != item.ingredientId || slot.empty) continue;
      const auto* ingredient = findIngredient(library, item.ingredientId);
      if (ingredient) {
        Leg leg;
        leg.channel = slot.index;
        leg.name = ingredient->name;
        leg.hue = hueFor(ingredient->colour, slot.index);
        leg.plannedMl = item.ml;
        legs.push_back(leg);
      }
      break;
    }
  }
  return legs;
}

float totalPlannedMl(const std::vector<Leg>& legs) {
  float total = 0.0f;
  for (const auto& leg : legs) total += leg.plannedMl;
  return total;
}

LegState legState(float deliveredMl, float plannedMl) {
  if (plannedMl <= 0.0f) return LegState::done;
  if (deliveredMl >= plannedMl) return LegState::done;
  if (deliveredMl > 0.0f) return LegState::pouring;
  return LegState::waiting;
}

std::vector<Segment> segments(const std::vector<Leg>& legs,
                              const std::vector<float>& deliveredMl) {
  std::vector<Segment> out;
  const float total = totalPlannedMl(legs);
  if (total <= 0.0f) return out;
  for (size_t i = 0; i < legs.size(); i++) {
    float delivered = deliveredAt(deliveredMl, i);
    if (delivered <= 0.0f) continue;
    if (delivered > legs[i].plannedMl) delivered = legs[i].plannedMl;
    out.push_back({legs[i].hue, delivered / total});
  }
  return out;
}

std::vector<Card> cards(const std::vector<Leg>& legs, const std::vector<float>& deliveredMl) {
  std::vector<Card> out;
  out.reserve(legs.size());
  for (size_t i = 0; i < legs.size(); i++) {
    out.push_back({legs[i].name, legs[i].hue,
                   legState(deliveredAt(deliveredMl, i), legs[i].plannedMl)});
  }
  return out;
}

std::string quantityWord(float fraction) {
  if (fraction >= 0.95f) return "all of it";
  if (fraction >= 0.60f) return "most";
  if (fraction >= 0.25f) return "about half";
  if (fraction > 0.0f) return "a splash";
  return "none";
}

std::vector<LedgerRow> ledger(const std::vector<Leg>& legs,
                              const std::vector<float>& deliveredMl) {
  std::vector<LedgerRow> out;
  out.reserve(legs.size());
  for (size_t i = 0; i < legs.size(); i++) {
    float fraction = 0.0f;
    if (legs[i].plannedMl > 0.0f) {
      fraction = deliveredAt(deliveredMl, i) / legs[i].plannedMl;
      if (fraction > 1.0f) fraction = 1.0f;
      if (fraction < 0.0f) fraction = 0.0f;
    }
    out.push_back({legs[i].name, legs[i].hue, fraction, quantityWord(fraction)});
  }
  return out;
}

std::string abortedTitle(const std::string& drinkName) { return "Part of a " + drinkName; }

float pauseBarFraction(uint32_t remainingSeconds, int windowSeconds) {
  if (windowSeconds <= 0) return 0.0f;
  float fraction = (float)remainingSeconds / (float)windowSeconds;
  if (fraction > 1.0f) return 1.0f;
  if (fraction < 0.0f) return 0.0f;
  return fraction;
}

}  // namespace hmi
