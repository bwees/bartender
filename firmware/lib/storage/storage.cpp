#include "storage.h"

#include <cJSON.h>

namespace bartender {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kVersionAbsent = 0;

bool getString(const cJSON* obj, const char* key, std::string& out) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
  out = item->valuestring;
  return true;
}

bool getFloat(const cJSON* obj, const char* key, float& out) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsNumber(item)) return false;
  out = static_cast<float>(item->valuedouble);
  return true;
}

bool getInt(const cJSON* obj, const char* key, int& out) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsNumber(item)) return false;
  out = static_cast<int>(item->valuedouble);
  return true;
}

bool getBool(const cJSON* obj, const char* key, bool& out) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsBool(item)) return false;
  out = cJSON_IsTrue(item);
  return true;
}

std::optional<float> getOptionalFloat(const cJSON* obj, const char* key) {
  float value;
  if (getFloat(obj, key, value)) return value;
  return std::nullopt;
}

std::optional<std::string> getOptionalString(const cJSON* obj, const char* key) {
  std::string value;
  if (getString(obj, key, value)) return value;
  return std::nullopt;
}

bool readVersion(const cJSON* root, int& versionOut) {
  if (!getInt(root, "version", versionOut)) versionOut = kVersionAbsent;
  return versionOut == kSchemaVersion;
}

template <typename T, typename ParseItem>
bool parseArray(const cJSON* root, const char* key, std::vector<T>& out, ParseItem parseItem) {
  const cJSON* array = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsArray(array)) return false;
  const cJSON* element = nullptr;
  cJSON_ArrayForEach(element, array) {
    T parsed;
    if (!parseItem(element, parsed)) return false;
    out.push_back(parsed);
  }
  return true;
}

bool parseIngredient(const cJSON* item, Ingredient& out) {
  if (!getString(item, "id", out.id) || !getString(item, "name", out.name) ||
      !getString(item, "colour", out.colour)) {
    return false;
  }
  out.abv = getOptionalFloat(item, "abv");
  // Optional on read so a card written before the rate moved onto the bottle
  // still loads; the struct's 4.2 stands in until the owner calibrates.
  getFloat(item, "mlPerSecond", out.mlPerSecond);
  return true;
}

bool parsePour(const cJSON* item, Pour& out) {
  return getString(item, "ingredient", out.ingredientId) && getFloat(item, "ml", out.ml);
}

bool parseRecipe(const cJSON* item, Recipe& out) {
  if (!getString(item, "id", out.id) || !getString(item, "name", out.name)) return false;
  out.image = getOptionalString(item, "image");
  return parseArray(item, "pours", out.pours, parsePour);
}

bool parseSlot(const cJSON* item, Slot& out) {
  if (!getInt(item, "index", out.index) || !getBool(item, "empty", out.empty)) return false;
  out.ingredientId = getOptionalString(item, "ingredient");
  return true;
}

cJSON* toJson(const std::string& value) { return cJSON_CreateString(value.c_str()); }

}  // namespace

std::optional<Library> parseLibrary(const std::string& content, int& versionOut) {
  versionOut = kVersionAbsent;
  cJSON* root = cJSON_Parse(content.c_str());
  if (root == nullptr) return std::nullopt;

  Library library;
  const bool ok = readVersion(root, versionOut) &&
                  parseArray(root, "ingredients", library.ingredients, parseIngredient) &&
                  parseArray(root, "recipes", library.recipes, parseRecipe);
  cJSON_Delete(root);

  if (!ok) return std::nullopt;
  return library;
}

std::optional<Config> parseConfig(const std::string& content, int& versionOut) {
  versionOut = kVersionAbsent;
  cJSON* root = cJSON_Parse(content.c_str());
  if (root == nullptr) return std::nullopt;

  Config config;
  const cJSON* pour = cJSON_GetObjectItemCaseSensitive(root, "pour");
  const cJSON* cup = cJSON_GetObjectItemCaseSensitive(root, "cupSensor");
  const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
  const cJSON* ring = cJSON_GetObjectItemCaseSensitive(root, "ring");
  const bool ok = readVersion(root, versionOut) &&
                  parseArray(root, "slots", config.slots, parseSlot) &&
                  getInt(root, "pumpCount", config.pumpCount) &&
                  getInt(pour, "concurrency", config.pour.concurrency) &&
                  getInt(pour, "pauseWindowSeconds", config.pour.pauseWindowSeconds) &&
                  getFloat(pour, "watchdogFactor", config.pour.watchdogFactor) &&
                  getBool(cup, "enabled", config.cupSensor.enabled) &&
                  getFloat(cup, "emptyMm", config.cupSensor.emptyMm) &&
                  getFloat(cup, "cupMm", config.cupSensor.cupMm) &&
                  getInt(cup, "debounceSamples", config.cupSensor.debounceSamples) &&
                  getString(wifi, "ssid", config.wifi.ssid) &&
                  getString(wifi, "passphrase", config.wifi.passphrase) &&
                  getString(root, "pin", config.pin) &&
                  getInt(ring, "pixels", config.ring.pixels) &&
                  getInt(ring, "brightness", config.ring.brightness);
  cJSON_Delete(root);

  if (!ok) return std::nullopt;
  return config;
}

std::string serializeLibrary(const Library& library) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", kSchemaVersion);
  cJSON* ingredients = cJSON_AddArrayToObject(root, "ingredients");
  for (const auto& ingredient : library.ingredients) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "id", toJson(ingredient.id));
    cJSON_AddItemToObject(item, "name", toJson(ingredient.name));
    cJSON_AddItemToObject(item, "colour", toJson(ingredient.colour));
    if (ingredient.abv) cJSON_AddNumberToObject(item, "abv", *ingredient.abv);
    cJSON_AddNumberToObject(item, "mlPerSecond", ingredient.mlPerSecond);
    cJSON_AddItemToArray(ingredients, item);
  }
  cJSON* recipes = cJSON_AddArrayToObject(root, "recipes");
  for (const auto& recipe : library.recipes) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "id", toJson(recipe.id));
    cJSON_AddItemToObject(item, "name", toJson(recipe.name));
    if (recipe.image) cJSON_AddItemToObject(item, "image", toJson(*recipe.image));
    cJSON* pours = cJSON_AddArrayToObject(item, "pours");
    for (const auto& pour : recipe.pours) {
      cJSON* pourItem = cJSON_CreateObject();
      cJSON_AddItemToObject(pourItem, "ingredient", toJson(pour.ingredientId));
      cJSON_AddNumberToObject(pourItem, "ml", pour.ml);
      cJSON_AddItemToArray(pours, pourItem);
    }
    cJSON_AddItemToArray(recipes, item);
  }
  char* printed = cJSON_Print(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

std::string serializeConfig(const Config& config) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", kSchemaVersion);
  cJSON* slots = cJSON_AddArrayToObject(root, "slots");
  for (const auto& slot : config.slots) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "index", slot.index);
    cJSON_AddItemToObject(item, "ingredient",
                          slot.ingredientId ? toJson(*slot.ingredientId) : cJSON_CreateNull());
    cJSON_AddBoolToObject(item, "empty", slot.empty);
    cJSON_AddItemToArray(slots, item);
  }
  cJSON_AddNumberToObject(root, "pumpCount", config.pumpCount);
  cJSON* pour = cJSON_AddObjectToObject(root, "pour");
  cJSON_AddNumberToObject(pour, "concurrency", config.pour.concurrency);
  cJSON_AddNumberToObject(pour, "pauseWindowSeconds", config.pour.pauseWindowSeconds);
  cJSON_AddNumberToObject(pour, "watchdogFactor", config.pour.watchdogFactor);
  cJSON* cup = cJSON_AddObjectToObject(root, "cupSensor");
  cJSON_AddBoolToObject(cup, "enabled", config.cupSensor.enabled);
  cJSON_AddNumberToObject(cup, "emptyMm", config.cupSensor.emptyMm);
  cJSON_AddNumberToObject(cup, "cupMm", config.cupSensor.cupMm);
  cJSON_AddNumberToObject(cup, "debounceSamples", config.cupSensor.debounceSamples);
  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddItemToObject(wifi, "ssid", toJson(config.wifi.ssid));
  cJSON_AddItemToObject(wifi, "passphrase", toJson(config.wifi.passphrase));
  cJSON_AddStringToObject(root, "pin", config.pin.c_str());
  cJSON* ring = cJSON_AddObjectToObject(root, "ring");
  cJSON_AddNumberToObject(ring, "pixels", config.ring.pixels);
  cJSON_AddNumberToObject(ring, "brightness", config.ring.brightness);
  char* printed = cJSON_Print(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

namespace {

bool saveWithBackup(const Filesystem& fs, const std::string& path, const std::string& content) {
  const std::string tmp = path + ".tmp";
  const std::string bak = path + ".bak";
  if (!fs.write(tmp, content)) return false;
  if (fs.exists(path)) {
    // FAT rename refuses an existing destination, so the stale backup must go first.
    fs.remove(bak);
    if (!fs.rename(path, bak)) {
      fs.remove(tmp);
      return false;
    }
  }
  if (!fs.rename(tmp, path)) {
    fs.remove(tmp);
    return false;
  }
  return true;
}

LoadStatus parseFailure(int version) {
  return version == kVersionAbsent || version == kSchemaVersion ? LoadStatus::parseError
                                                                : LoadStatus::badVersion;
}

template <typename ParseFn>
LoadStatus loadWithParse(const Filesystem& fs, const std::string& path, ParseFn parse) {
  LoadStatus last = LoadStatus::missing;
  for (const std::string& candidate : {path, path + ".bak"}) {
    auto content = fs.read(candidate);
    if (!content) continue;
    int version = kVersionAbsent;
    if (parse(*content, version)) return LoadStatus::ok;
    last = parseFailure(version);
  }
  return last;
}

}  // namespace

bool saveLibrary(const Filesystem& fs, const std::string& path, const Library& library) {
  return saveWithBackup(fs, path, serializeLibrary(library));
}

bool saveConfig(const Filesystem& fs, const std::string& path, const Config& config) {
  return saveWithBackup(fs, path, serializeConfig(config));
}

LoadStatus loadLibrary(const Filesystem& fs, const std::string& path, Library& out) {
  return loadWithParse(fs, path, [&out](const std::string& content, int& version) {
    auto parsed = parseLibrary(content, version);
    if (!parsed) return false;
    out = *parsed;
    return true;
  });
}

LoadStatus loadConfig(const Filesystem& fs, const std::string& path, Config& out) {
  return loadWithParse(fs, path, [&out](const std::string& content, int& version) {
    auto parsed = parseConfig(content, version);
    if (!parsed) return false;
    out = *parsed;
    return true;
  });
}

bool servable(const Recipe& recipe, const std::vector<Slot>& slots) {
  for (const auto& pour : recipe.pours) {
    bool found = false;
    for (const auto& slot : slots) {
      if (!slot.empty && slot.ingredientId && *slot.ingredientId == pour.ingredientId) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

namespace {

const char* const kGinId = "7c9e6679-7425-40de-944b-e07fc1f90ae7";
const char* const kCampariId = "b1f0ca41-2b28-4f8e-9e0c-6b1f2a3d4e5f";
const char* const kVermouthId = "c4a2d8e1-9f30-4a17-b6d8-3e5f7a9b0c1d";
const char* const kNegroniId = "d3b07384-d9a0-4b7c-8f1c-2a3b4c5d6e7f";

}  // namespace

Config defaultConfig(const std::string& apSuffix) {
  Config config;
  config.slots = {
    {0, std::string(kGinId), false},
    {1, std::string(kCampariId), false},
    {2, std::string(kVermouthId), false},
    {3, std::nullopt, false},
  };
  config.pumpCount = 8;
  config.pour = {1, 30, 1.5f};
  config.cupSensor = {true, 95.0f, 45.0f, 3};
  config.wifi = {"Bartender-" + apSuffix, "change-me"};
  config.pin = "1234";
  config.ring = {16, 60};
  return config;
}

Library defaultLibrary() {
  Library library;
  library.ingredients = {
    {kGinId, "Gin", "#94C7C1", 40.0f, 4.2f},
    {kCampariId, "Campari", "#C0392B", 25.0f, 4.2f},
    {kVermouthId, "Sweet vermouth", "#8E3B46", 16.0f, 4.2f},
  };
  Recipe negroni;
  negroni.id = kNegroniId;
  negroni.name = "Negroni";
  negroni.image = std::string("/assets/") + kNegroniId + ".jpeg";
  negroni.pours = {{kGinId, 30.0f}, {kCampariId, 30.0f}, {kVermouthId, 30.0f}};
  library.recipes = {negroni};
  return library;
}

}  // namespace bartender
