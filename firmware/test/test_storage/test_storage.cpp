#include <unistd.h>
#include <unity.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "domain.h"
#include "storage.h"

class HostFs : public bartender::Filesystem {
 public:
  std::string root;

  std::optional<std::string> read(const std::string& path) const override {
    std::ifstream file(root + path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  }
  bool write(const std::string& path, const std::string& content) const override {
    std::ofstream file(root + path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << content;
    return file.good();
  }
  bool rename(const std::string& from, const std::string& to) const override {
    std::error_code error;
    std::filesystem::rename(root + from, root + to, error);
    return !error;
  }
  bool remove(const std::string& path) const override {
    std::error_code error;
    return std::filesystem::remove(root + path, error);
  }
  bool exists(const std::string& path) const override {
    return std::filesystem::is_regular_file(root + path);
  }
};

// The device card is FAT, where rename refuses an existing destination and the
// host filesystem silently overwrites. `renameRefusesExistingTarget` reproduces
// the card's behaviour so the atomic-write ordering is tested against it.
class MemFs : public bartender::Filesystem {
 public:
  mutable std::map<std::string, std::string> files;
  mutable std::vector<std::string> ops;
  std::string renameFailsFrom;
  bool renameRefusesExistingTarget = false;

  std::optional<std::string> read(const std::string& path) const override {
    auto it = files.find(path);
    if (it == files.end()) return std::nullopt;
    return it->second;
  }
  bool write(const std::string& path, const std::string& content) const override {
    ops.push_back("write " + path);
    files[path] = content;
    return true;
  }
  bool rename(const std::string& from, const std::string& to) const override {
    ops.push_back("rename " + from + " -> " + to);
    if (from == renameFailsFrom) return false;
    auto it = files.find(from);
    if (it == files.end()) return false;
    if (renameRefusesExistingTarget && files.count(to) != 0) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  bool remove(const std::string& path) const override {
    ops.push_back("remove " + path);
    return files.erase(path) != 0;
  }
  bool exists(const std::string& path) const override { return files.count(path) != 0; }
};

HostFs fs;

const char* kLibraryJson = R"({
  "version": 1,
  "ingredients": [
    {"id": "ing-1", "name": "Gin", "colour": "#94C7C1", "abv": 40.0, "mlPerSecond": 3.1},
    {"id": "ing-2", "name": "Campari", "colour": "#C0392B"}
  ],
  "recipes": [
    {"id": "rec-1", "name": "Negroni", "image": "/assets/rec-1.jpeg",
     "pours": [
       {"ingredient": "ing-1", "ml": 30},
       {"ingredient": "ing-2", "ml": 30}
     ]}
  ]
})";

const char* kConfigJson = R"({
  "version": 1,
  "slots": [
    {"index": 0, "ingredient": "ing-1", "empty": false},
    {"index": 1, "ingredient": null, "empty": false}
  ],
  "pumpCount": 8,
  "pour": {"concurrency": 3, "pauseWindowSeconds": 30, "watchdogFactor": 1.5},
  "cupSensor": {"enabled": true, "emptyMm": 95.0, "cupMm": 45.0, "debounceSamples": 3},
  "wifi": {"ssid": "Bartender-A3F2", "passphrase": "secret"},
  "pin": "1234",
  "ring": {"pixels": 16, "brightness": 60}
})";

// What a card written before the rate moved onto the bottle looks like: a
// channel-keyed calibration array and no per-ingredient
// rate. It has to keep loading, or the owner loses their recipes to a silent
// fall back to defaults.
const char* kLegacyConfigJson = R"({
  "version": 1,
  "slots": [{"index": 0, "ingredient": "ing-1", "empty": false}],
  "pumpCount": 8,
  "calibration": [{"channel": 0, "mlPerSecond": 4.2}],
  "pour": {"concurrency": 1, "pauseWindowSeconds": 30, "watchdogFactor": 1.5},
  "cupSensor": {"enabled": true, "emptyMm": 95.0, "cupMm": 45.0, "debounceSamples": 3},
  "wifi": {"ssid": "Bartender-A3F2", "passphrase": "secret"},
  "pin": "1234",
  "ring": {"pixels": 16, "brightness": 60}
})";

bartender::Library libraryFrom(const char* json) {
  int version = 0;
  auto parsed = bartender::parseLibrary(json, version);
  TEST_ASSERT_TRUE(parsed.has_value());
  return *parsed;
}

void setUp() {
  fs.root = "/tmp/bartender-test-" + std::to_string(getpid());
  std::filesystem::remove_all(fs.root);
  std::filesystem::create_directories(fs.root);
}
void tearDown() { std::filesystem::remove_all(fs.root); }

void test_parse_library_extracts_fields() {
  int version = 0;
  auto library = bartender::parseLibrary(kLibraryJson, version);
  TEST_ASSERT_TRUE(library.has_value());
  TEST_ASSERT_EQUAL_INT(1, version);
  TEST_ASSERT_EQUAL_size_t(2, library->ingredients.size());
  TEST_ASSERT_EQUAL_STRING("Gin", library->ingredients[0].name.c_str());
  TEST_ASSERT_TRUE(library->ingredients[0].abv.has_value());
  TEST_ASSERT_EQUAL_FLOAT(40.0, *library->ingredients[0].abv);
  TEST_ASSERT_FALSE(library->ingredients[1].abv.has_value());
  TEST_ASSERT_EQUAL_size_t(1, library->recipes.size());
  TEST_ASSERT_EQUAL_size_t(2, library->recipes[0].pours.size());
  TEST_ASSERT_EQUAL_FLOAT(30.0, library->recipes[0].pours[0].ml);
  TEST_ASSERT_EQUAL_FLOAT(3.1f, library->ingredients[0].mlPerSecond);
  TEST_ASSERT_EQUAL_FLOAT(4.2f, library->ingredients[1].mlPerSecond);
}

void test_parse_library_rejects_unknown_version() {
  int version = 0;
  auto library = bartender::parseLibrary(
      R"({"version": 99, "ingredients": [], "recipes": []})", version);
  TEST_ASSERT_FALSE(library.has_value());
  TEST_ASSERT_EQUAL_INT(99, version);
}

void test_parse_library_reports_absent_version_as_zero() {
  int version = 7;
  auto library = bartender::parseLibrary(R"({"ingredients": [], "recipes": []})", version);
  TEST_ASSERT_FALSE(library.has_value());
  TEST_ASSERT_EQUAL_INT(0, version);
}

void test_parse_library_rejects_malformed_json() {
  int version = 0;
  auto library = bartender::parseLibrary("{not json", version);
  TEST_ASSERT_FALSE(library.has_value());
  TEST_ASSERT_EQUAL_INT(0, version);
}

void test_parse_library_rejects_missing_required_field() {
  int version = 0;
  auto library =
      bartender::parseLibrary(R"({"version": 1, "ingredients": [{"id": "x"}], "recipes": []})",
                              version);
  TEST_ASSERT_FALSE(library.has_value());
}

void test_parse_library_rejects_bad_ingredient_alongside_valid_recipe() {
  int version = 0;
  auto library = bartender::parseLibrary(R"({
    "version": 1,
    "ingredients": [{"id": "ing-1", "name": "Gin"}],
    "recipes": [{"id": "rec-1", "name": "Negroni", "pours": []}]
  })",
                                         version);
  TEST_ASSERT_FALSE(library.has_value());
}

void test_parse_library_rejects_bad_pour_alongside_valid_ingredients() {
  int version = 0;
  auto library = bartender::parseLibrary(R"({
    "version": 1,
    "ingredients": [{"id": "ing-1", "name": "Gin", "colour": "#94C7C1"}],
    "recipes": [{"id": "rec-1", "name": "Negroni", "pours": [{"ingredient": "ing-1"}]}]
  })",
                                         version);
  TEST_ASSERT_FALSE(library.has_value());
}

void test_parse_library_requires_both_arrays() {
  int version = 0;
  TEST_ASSERT_FALSE(
      bartender::parseLibrary(R"({"version": 1, "recipes": []})", version).has_value());
  TEST_ASSERT_FALSE(
      bartender::parseLibrary(R"({"version": 1, "ingredients": []})", version).has_value());
  TEST_ASSERT_FALSE(
      bartender::parseLibrary(R"({"version": 1, "ingredients": {}, "recipes": []})", version)
          .has_value());
}

void test_parse_config_rejects_unknown_version() {
  int version = 0;
  auto config = bartender::parseConfig(R"({"version": 2})", version);
  TEST_ASSERT_FALSE(config.has_value());
  TEST_ASSERT_EQUAL_INT(2, version);
}

void test_parse_config_rejects_missing_section() {
  const char* sections[] = {"\"pour\"", "\"cupSensor\"", "\"wifi\"", "\"ring\"", "\"slots\"",
                            "\"pumpCount\"", "\"pin\""};
  for (const char* section : sections) {
    std::string json = kConfigJson;
    const size_t at = json.find(section);
    TEST_ASSERT_TRUE(at != std::string::npos);
    json.replace(at, std::string(section).size(), "\"unused\"");
    int version = 0;
    TEST_ASSERT_FALSE(bartender::parseConfig(json, version).has_value());
  }
}

void test_config_round_trip() {
  int version = 0;
  auto config = bartender::parseConfig(kConfigJson, version);
  TEST_ASSERT_TRUE(config.has_value());
  std::string serialized = bartender::serializeConfig(*config);
  int version2 = 0;
  auto reparsed = bartender::parseConfig(serialized, version2);
  TEST_ASSERT_TRUE(reparsed.has_value());
  TEST_ASSERT_EQUAL_INT(8, reparsed->pumpCount);
  TEST_ASSERT_EQUAL_INT(3, reparsed->pour.concurrency);
  TEST_ASSERT_EQUAL_INT(30, reparsed->pour.pauseWindowSeconds);
  TEST_ASSERT_EQUAL_FLOAT(1.5, reparsed->pour.watchdogFactor);
  TEST_ASSERT_TRUE(reparsed->cupSensor.enabled);
  TEST_ASSERT_EQUAL_FLOAT(95.0, reparsed->cupSensor.emptyMm);
  TEST_ASSERT_EQUAL_FLOAT(45.0, reparsed->cupSensor.cupMm);
  TEST_ASSERT_EQUAL_INT(3, reparsed->cupSensor.debounceSamples);
  TEST_ASSERT_EQUAL_STRING("Bartender-A3F2", reparsed->wifi.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret", reparsed->wifi.passphrase.c_str());
  TEST_ASSERT_TRUE(reparsed->slots[0].ingredientId.has_value());
  TEST_ASSERT_FALSE(reparsed->slots[1].ingredientId.has_value());
  TEST_ASSERT_EQUAL_STRING("1234", reparsed->pin.c_str());
  TEST_ASSERT_EQUAL_INT(16, reparsed->ring.pixels);
  TEST_ASSERT_EQUAL_INT(60, reparsed->ring.brightness);
  // The screen has its own field now: it used to be written over the LED ring's.
}

void test_the_screen_brightness_no_longer_shares_the_rings_field() {
  int version = 0;
  auto config = bartender::parseConfig(kConfigJson, version);
  TEST_ASSERT_TRUE(config.has_value());
  TEST_ASSERT_EQUAL_INT(60, config->ring.brightness);
}

void test_a_card_written_before_the_split_still_loads() {
  int version = 0;
  auto config = bartender::parseConfig(kLegacyConfigJson, version);
  TEST_ASSERT_TRUE(config.has_value());
  TEST_ASSERT_EQUAL_INT(1, version);
  TEST_ASSERT_EQUAL_INT(8, config->pumpCount);

  auto library = bartender::parseLibrary(
      R"({"version": 1, "ingredients": [{"id": "ing-1", "name": "Gin", "colour": "#94C7C1"}],
          "recipes": []})",
      version);
  TEST_ASSERT_TRUE(library.has_value());
  TEST_ASSERT_EQUAL_FLOAT(4.2f, library->ingredients[0].mlPerSecond);
}

void test_a_rewritten_config_drops_the_channel_keyed_calibration() {
  int version = 0;
  auto config = bartender::parseConfig(kLegacyConfigJson, version);
  TEST_ASSERT_TRUE(config.has_value());
  const std::string serialized = bartender::serializeConfig(*config);
  TEST_ASSERT_TRUE(serialized.find("calibration") == std::string::npos);
}

void test_library_round_trip() {
  int version = 0;
  auto library = bartender::parseLibrary(kLibraryJson, version);
  TEST_ASSERT_TRUE(library.has_value());
  auto reparsed =
      bartender::parseLibrary(bartender::serializeLibrary(*library), version);
  TEST_ASSERT_TRUE(reparsed.has_value());
  TEST_ASSERT_EQUAL_size_t(2, reparsed->ingredients.size());
  TEST_ASSERT_EQUAL_size_t(1, reparsed->recipes.size());
  TEST_ASSERT_TRUE(reparsed->recipes[0].image.has_value());
  TEST_ASSERT_EQUAL_STRING("/assets/rec-1.jpeg", reparsed->recipes[0].image->c_str());
  TEST_ASSERT_FALSE(reparsed->ingredients[1].abv.has_value());
  TEST_ASSERT_EQUAL_FLOAT(3.1f, reparsed->ingredients[0].mlPerSecond);
}

void test_save_and_load_round_trips_through_the_filesystem() {
  const bartender::Library library = libraryFrom(kLibraryJson);
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", library));
  TEST_ASSERT_FALSE(fs.exists("/recipes.json.tmp"));

  bartender::Library loaded;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadLibrary(fs, "/recipes.json", loaded));
  TEST_ASSERT_EQUAL_size_t(2, loaded.ingredients.size());

  int version = 0;
  auto config = bartender::parseConfig(kConfigJson, version);
  TEST_ASSERT_TRUE(bartender::saveConfig(fs, "/config.json", *config));
  bartender::Config loadedConfig;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadConfig(fs, "/config.json", loadedConfig));
  TEST_ASSERT_EQUAL_INT(8, loadedConfig.pumpCount);
  TEST_ASSERT_EQUAL_size_t(2, loadedConfig.slots.size());
}

void test_first_save_writes_no_backup() {
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", libraryFrom(kLibraryJson)));
  TEST_ASSERT_FALSE(fs.exists("/recipes.json.bak"));
}

void test_second_save_keeps_the_previous_copy_as_backup() {
  bartender::Library first = libraryFrom(kLibraryJson);
  bartender::Library second = first;
  second.ingredients.pop_back();

  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", first));
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", second));
  TEST_ASSERT_FALSE(fs.exists("/recipes.json.tmp"));

  int version = 0;
  auto backup = bartender::parseLibrary(*fs.read("/recipes.json.bak"), version);
  TEST_ASSERT_TRUE(backup.has_value());
  TEST_ASSERT_EQUAL_size_t(2, backup->ingredients.size());
  auto current = bartender::parseLibrary(*fs.read("/recipes.json"), version);
  TEST_ASSERT_TRUE(current.has_value());
  TEST_ASSERT_EQUAL_size_t(1, current->ingredients.size());
}

void test_save_writes_tmp_before_touching_the_target() {
  MemFs mem;
  TEST_ASSERT_TRUE(bartender::saveLibrary(mem, "/recipes.json", libraryFrom(kLibraryJson)));
  TEST_ASSERT_TRUE(mem.ops.size() >= 2);
  TEST_ASSERT_EQUAL_STRING("write /recipes.json.tmp", mem.ops[0].c_str());
  TEST_ASSERT_EQUAL_STRING("rename /recipes.json.tmp -> /recipes.json", mem.ops[1].c_str());
}

void test_repeated_saves_survive_a_fat_rename_that_refuses_an_existing_target() {
  MemFs mem;
  mem.renameRefusesExistingTarget = true;
  const bartender::Library library = libraryFrom(kLibraryJson);
  for (int attempt = 0; attempt < 3; ++attempt) {
    TEST_ASSERT_TRUE(bartender::saveLibrary(mem, "/recipes.json", library));
  }
  TEST_ASSERT_FALSE(mem.exists("/recipes.json.tmp"));
  bartender::Library loaded;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadLibrary(mem, "/recipes.json", loaded));
}

void test_failed_save_leaves_the_previous_copy_recoverable() {
  MemFs mem;
  bartender::Library first = libraryFrom(kLibraryJson);
  bartender::Library second = first;
  second.ingredients.pop_back();

  TEST_ASSERT_TRUE(bartender::saveLibrary(mem, "/recipes.json", first));
  mem.renameFailsFrom = "/recipes.json.tmp";
  TEST_ASSERT_FALSE(bartender::saveLibrary(mem, "/recipes.json", second));
  TEST_ASSERT_FALSE(mem.exists("/recipes.json.tmp"));

  bartender::Library recovered;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadLibrary(mem, "/recipes.json", recovered));
  TEST_ASSERT_EQUAL_size_t(2, recovered.ingredients.size());
}

void test_corrupt_primary_falls_back_to_the_backup() {
  bartender::Library library = libraryFrom(kLibraryJson);
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", library));
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", library));
  TEST_ASSERT_TRUE(fs.write("/recipes.json", "{corrupted"));

  bartender::Library recovered;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadLibrary(fs, "/recipes.json", recovered));
  TEST_ASSERT_EQUAL_size_t(2, recovered.ingredients.size());
}

void test_load_prefers_the_primary_over_the_backup() {
  bartender::Library first = libraryFrom(kLibraryJson);
  bartender::Library second = first;
  second.ingredients.pop_back();
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", first));
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", second));

  bartender::Library loaded;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::ok,
                        bartender::loadLibrary(fs, "/recipes.json", loaded));
  TEST_ASSERT_EQUAL_size_t(1, loaded.ingredients.size());
}

void test_load_missing_reports_missing() {
  bartender::Library library;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::missing,
                        bartender::loadLibrary(fs, "/recipes.json", library));
}

void test_load_reports_bad_version_when_every_copy_is_unknown() {
  TEST_ASSERT_TRUE(
      fs.write("/recipes.json", R"({"version": 99, "ingredients": [], "recipes": []})"));
  TEST_ASSERT_TRUE(
      fs.write("/recipes.json.bak", R"({"version": 98, "ingredients": [], "recipes": []})"));
  bartender::Library library;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::badVersion,
                        bartender::loadLibrary(fs, "/recipes.json", library));

  TEST_ASSERT_TRUE(fs.write("/config.json", R"({"version": 99})"));
  bartender::Config config;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::badVersion,
                        bartender::loadConfig(fs, "/config.json", config));
}

void test_load_reports_parse_error_for_a_known_version_with_a_bad_body() {
  TEST_ASSERT_TRUE(
      fs.write("/recipes.json", R"({"version": 1, "ingredients": [{"id": "x"}], "recipes": []})"));
  bartender::Library library;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::parseError,
                        bartender::loadLibrary(fs, "/recipes.json", library));
}

void test_load_reports_parse_error_when_every_copy_is_corrupt() {
  TEST_ASSERT_TRUE(fs.write("/recipes.json", "{corrupted"));
  TEST_ASSERT_TRUE(fs.write("/recipes.json.bak", "also not json"));
  bartender::Library library;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::parseError,
                        bartender::loadLibrary(fs, "/recipes.json", library));
}

void test_load_reports_the_backup_failure_when_the_primary_is_unreadable() {
  TEST_ASSERT_TRUE(fs.write("/recipes.json.bak", R"({"version": 99})"));
  bartender::Library library;
  TEST_ASSERT_EQUAL_INT(bartender::LoadStatus::badVersion,
                        bartender::loadLibrary(fs, "/recipes.json", library));
}

void test_servable_rules() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}, {"ing-2", 30.0f}};

  std::vector<bartender::Slot> bothAssigned = {
      {0, std::optional<std::string>("ing-1"), false},
      {1, std::optional<std::string>("ing-2"), false}};
  TEST_ASSERT_TRUE(bartender::servable(recipe, bothAssigned));

  std::vector<bartender::Slot> oneEmpty = {
      {0, std::optional<std::string>("ing-1"), false},
      {1, std::optional<std::string>("ing-2"), true}};
  TEST_ASSERT_FALSE(bartender::servable(recipe, oneEmpty));

  std::vector<bartender::Slot> missingIngredient = {
      {0, std::optional<std::string>("ing-1"), false}};
  TEST_ASSERT_FALSE(bartender::servable(recipe, missingIngredient));

  std::vector<bartender::Slot> unassigned = {{0, std::nullopt, false}};
  TEST_ASSERT_FALSE(bartender::servable(recipe, unassigned));

  bartender::Recipe noPours;
  TEST_ASSERT_TRUE(bartender::servable(noPours, {}));
}

void test_servable_accepts_a_duplicate_ingredient_in_a_second_stocked_slot() {
  bartender::Recipe recipe;
  recipe.pours = {{"ing-1", 30.0f}};
  std::vector<bartender::Slot> slots = {{0, std::optional<std::string>("ing-1"), true},
                                        {1, std::optional<std::string>("ing-1"), false}};
  TEST_ASSERT_TRUE(bartender::servable(recipe, slots));
}

#ifdef SDCARD_DIR
std::string readCardFile(const char* name) {
  std::ifstream file(std::string(SDCARD_DIR) + "/" + name, std::ios::binary);
  TEST_ASSERT_TRUE(file.good());
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void test_shipped_card_template_parses_and_round_trips() {
  int version = 0;
  auto library = bartender::parseLibrary(readCardFile("recipes.json"), version);
  TEST_ASSERT_TRUE(library.has_value());
  TEST_ASSERT_EQUAL_INT(1, version);
  TEST_ASSERT_TRUE(bartender::parseLibrary(bartender::serializeLibrary(*library), version)
                       .has_value());

  auto config = bartender::parseConfig(readCardFile("config.json"), version);
  TEST_ASSERT_TRUE(config.has_value());
  TEST_ASSERT_EQUAL_INT(1, version);
  TEST_ASSERT_TRUE(
      bartender::parseConfig(bartender::serializeConfig(*config), version).has_value());

  for (const auto& recipe : library->recipes) {
    TEST_ASSERT_TRUE(bartender::servable(recipe, config->slots));
  }
}
#endif


void test_default_config_carries_the_mac_suffix_into_the_ssid() {
  const auto config = bartender::defaultConfig("A3F2");
  TEST_ASSERT_EQUAL_STRING("Bartender-A3F2", config.wifi.ssid.c_str());
  TEST_ASSERT_FALSE(config.wifi.passphrase.empty());
}

void test_default_library_is_servable_against_default_config_slots() {
  const auto config = bartender::defaultConfig("0000");
  const auto library = bartender::defaultLibrary();
  TEST_ASSERT_FALSE(library.recipes.empty());
  for (const auto& recipe : library.recipes) {
    TEST_ASSERT_TRUE(bartender::servable(recipe, config.slots));
  }
}

void test_defaults_round_trip_through_the_filesystem() {
  MemFs fs;
  fs.renameRefusesExistingTarget = true;
  TEST_ASSERT_TRUE(bartender::saveConfig(fs, "/config.json", bartender::defaultConfig("BEEF")));
  TEST_ASSERT_TRUE(bartender::saveLibrary(fs, "/recipes.json", bartender::defaultLibrary()));

  bartender::Config config;
  bartender::Library library;
  TEST_ASSERT_EQUAL(bartender::LoadStatus::ok,
                    bartender::loadConfig(fs, "/config.json", config));
  TEST_ASSERT_EQUAL(bartender::LoadStatus::ok,
                    bartender::loadLibrary(fs, "/recipes.json", library));
  TEST_ASSERT_EQUAL_STRING("Bartender-BEEF", config.wifi.ssid.c_str());
  TEST_ASSERT_EQUAL(1, (int)library.recipes.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_library_extracts_fields);
  RUN_TEST(test_parse_library_rejects_unknown_version);
  RUN_TEST(test_parse_library_reports_absent_version_as_zero);
  RUN_TEST(test_parse_library_rejects_malformed_json);
  RUN_TEST(test_parse_library_rejects_missing_required_field);
  RUN_TEST(test_parse_library_rejects_bad_ingredient_alongside_valid_recipe);
  RUN_TEST(test_parse_library_rejects_bad_pour_alongside_valid_ingredients);
  RUN_TEST(test_parse_library_requires_both_arrays);
  RUN_TEST(test_parse_config_rejects_unknown_version);
  RUN_TEST(test_parse_config_rejects_missing_section);
  RUN_TEST(test_config_round_trip);
  RUN_TEST(test_the_screen_brightness_no_longer_shares_the_rings_field);
  RUN_TEST(test_a_card_written_before_the_split_still_loads);
  RUN_TEST(test_a_rewritten_config_drops_the_channel_keyed_calibration);
  RUN_TEST(test_library_round_trip);
  RUN_TEST(test_save_and_load_round_trips_through_the_filesystem);
  RUN_TEST(test_first_save_writes_no_backup);
  RUN_TEST(test_second_save_keeps_the_previous_copy_as_backup);
  RUN_TEST(test_save_writes_tmp_before_touching_the_target);
  RUN_TEST(test_repeated_saves_survive_a_fat_rename_that_refuses_an_existing_target);
  RUN_TEST(test_failed_save_leaves_the_previous_copy_recoverable);
  RUN_TEST(test_corrupt_primary_falls_back_to_the_backup);
  RUN_TEST(test_load_prefers_the_primary_over_the_backup);
  RUN_TEST(test_load_missing_reports_missing);
  RUN_TEST(test_load_reports_bad_version_when_every_copy_is_unknown);
  RUN_TEST(test_load_reports_parse_error_for_a_known_version_with_a_bad_body);
  RUN_TEST(test_load_reports_parse_error_when_every_copy_is_corrupt);
  RUN_TEST(test_load_reports_the_backup_failure_when_the_primary_is_unreadable);
  RUN_TEST(test_servable_rules);
  RUN_TEST(test_servable_accepts_a_duplicate_ingredient_in_a_second_stocked_slot);
  RUN_TEST(test_default_config_carries_the_mac_suffix_into_the_ssid);
  RUN_TEST(test_default_library_is_servable_against_default_config_slots);
  RUN_TEST(test_defaults_round_trip_through_the_filesystem);
#ifdef SDCARD_DIR
  RUN_TEST(test_shipped_card_template_parses_and_round_trips);
#endif
  return UNITY_END();
}
