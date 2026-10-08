#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hmi {
namespace {

constexpr float kMinCupMm = 5.0f;
constexpr float kMaxCupMm = 400.0f;
constexpr float kCupThresholdGapMm = 5.0f;
constexpr float kCupStepMm = 5.0f;

int clampTenths(int tenths) {
  if (tenths < kDoseSliderMin) return kDoseSliderMin;
  if (tenths > kDoseSliderMax) return kDoseSliderMax;
  return tenths;
}

bool byName(const bartender::Ingredient& a, const bartender::Ingredient& b) {
  const size_t n = std::min(a.name.size(), b.name.size());
  for (size_t i = 0; i < n; i++) {
    const int ca = std::tolower((unsigned char)a.name[i]);
    const int cb = std::tolower((unsigned char)b.name[i]);
    if (ca != cb) return ca < cb;
  }
  return a.name.size() < b.name.size();
}

bartender::Slot& slotRow(bartender::Config& config, int index) {
  for (auto& slot : config.slots) {
    if (slot.index == index) return slot;
  }
  bartender::Slot fresh;
  fresh.index = index;
  config.slots.push_back(fresh);
  return config.slots.back();
}

}  // namespace

SlotChoices slotChoices(const bartender::Library& library) {
  std::vector<bartender::Ingredient> sorted = library.ingredients;
  std::stable_sort(sorted.begin(), sorted.end(), byName);

  SlotChoices choices;
  choices.options = "Not assigned";
  choices.ids.push_back(std::string());
  for (const auto& ingredient : sorted) {
    choices.options += "\n" + (ingredient.name.empty() ? ingredient.id : ingredient.name);
    choices.ids.push_back(ingredient.id);
  }
  return choices;
}

int choiceOf(const SlotChoices& choices, const std::optional<std::string>& ingredientId) {
  if (!ingredientId) return 0;
  for (size_t i = 0; i < choices.ids.size(); i++) {
    if (choices.ids[i] == *ingredientId) return (int)i;
  }
  return 0;
}

void assignSlot(bartender::Config& config, int index,
                const std::optional<std::string>& ingredientId) {
  if (ingredientId) {
    for (auto& slot : config.slots) {
      if (slot.index == index) continue;
      if (!slot.ingredientId || *slot.ingredientId != *ingredientId) continue;
      slot.ingredientId.reset();
      slot.empty = false;
    }
  }
  bartender::Slot& slot = slotRow(config, index);
  if (slot.ingredientId != ingredientId) slot.empty = false;
  slot.ingredientId = ingredientId;
}

void markSlotEmpty(bartender::Config& config, int index, bool empty) {
  slotRow(config, index).empty = empty;
}

float calibrationOf(const bartender::Library& library, const std::string& ingredientId) {
  for (const auto& ingredient : library.ingredients) {
    if (ingredient.id == ingredientId) return ingredient.mlPerSecond;
  }
  return 0.0f;
}

void setCalibration(bartender::Library& library, const std::string& ingredientId,
                    float mlPerSecond) {
  for (auto& ingredient : library.ingredients) {
    if (ingredient.id != ingredientId) continue;
    ingredient.mlPerSecond = mlPerSecond;
    return;
  }
}

float nudgeMlPerSecond(float mlPerSecond, Dose dose) {
  return (float)clampTenths((int)std::lround(mlPerSecond * 10.0f) +
                            (dose == Dose::more ? -1 : 1)) /
         10.0f;
}

int doseSlider(float mlPerSecond) {
  return kDoseSliderMin + kDoseSliderMax - clampTenths((int)std::lround(mlPerSecond * 10.0f));
}

float doseSliderRate(int position) {
  return (float)(kDoseSliderMin + kDoseSliderMax - clampTenths(position)) / 10.0f;
}

float clampCupMm(float mm) {
  if (mm < kMinCupMm) return kMinCupMm;
  if (mm > kMaxCupMm) return kMaxCupMm;
  return mm;
}

float nudgeCupMm(float mm, int steps) { return clampCupMm(mm + (float)steps * kCupStepMm); }

int clampDebounce(int samples) {
  if (samples < 1) return 1;
  if (samples > 10) return 10;
  return samples;
}

bool cupThresholdsSane(const bartender::CupSensorConfig& sensor) {
  return sensor.cupMm + kCupThresholdGapMm <= sensor.emptyMm;
}

bool wifiSsidValid(const std::string& ssid) {
  return !ssid.empty() && ssid.size() <= 32;
}

bool wifiPassphraseValid(const std::string& passphrase) {
  return passphrase.size() >= 8 && passphrase.size() <= 63;
}

}  // namespace hmi
