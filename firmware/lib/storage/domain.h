#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bartender {

struct Ingredient {
  std::string id;
  std::string name;
  std::string colour;
  std::optional<float> abv;
  // Calibration follows the bottle, not the pump: a gin line moved to another
  // channel keeps the rate it was measured at.
  float mlPerSecond = 4.2f;
};

struct Pour {
  std::string ingredientId;
  float ml = 0.0f;
};

struct Recipe {
  std::string id;
  std::string name;
  std::optional<std::string> image;
  std::vector<Pour> pours;
};

struct Library {
  std::vector<Ingredient> ingredients;
  std::vector<Recipe> recipes;
};

struct Slot {
  int index = 0;
  std::optional<std::string> ingredientId;
  bool empty = false;
};

struct PourConfig {
  int concurrency = 1;
  int pauseWindowSeconds = 0;
  float watchdogFactor = 1.0f;
};

struct CupSensorConfig {
  bool enabled = false;
  float emptyMm = 0.0f;
  float cupMm = 0.0f;
  int debounceSamples = 0;
};

struct WifiConfig {
  std::string ssid;
  std::string passphrase;
};

struct RingConfig {
  int pixels = 0;
  int brightness = 0;
};

struct Config {
  std::vector<Slot> slots;
  int pumpCount = 0;
  PourConfig pour;
  CupSensorConfig cupSensor;
  WifiConfig wifi;
  std::string pin;
  RingConfig ring;
};

bool servable(const Recipe& recipe, const std::vector<Slot>& slots);

}  // namespace bartender
