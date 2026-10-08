#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain.h"

namespace hmi {

// An lv_dropdown's newline-joined option list and the ingredient behind each row.
// Row 0 is always "Not assigned", so ids[0] is empty.
struct SlotChoices {
  std::string options;
  std::vector<std::string> ids;
};

SlotChoices slotChoices(const bartender::Library& library);

int choiceOf(const SlotChoices& choices, const std::optional<std::string>& ingredientId);

// A bottle stands in one place, so assigning it to a slot clears it from any
// other. Creates the slot row when config.slots has none for the index, which the
// default config leaves the case for every channel past the third.
void assignSlot(bartender::Config& config, int index,
                const std::optional<std::string>& ingredientId);

void markSlotEmpty(bartender::Config& config, int index, bool empty);

// Keyed by bottle, not by channel: an ingredient carries its own rate wherever
// it is plugged in. An id no bottle answers to reads 0, which is a rate nothing
// will pour at.
float calibrationOf(const bartender::Library& library, const std::string& ingredientId);

void setCalibration(bartender::Library& library, const std::string& ingredientId,
                    float mlPerSecond);

// Nobody knows their pump's ml/s, but everyone can see that the test dose came
// up short, so the screen asks for MORE or LESS liquid and this turns that into
// a rate. It inverts: the dose runs for mlToMs(ml, mlPerSecond), so a pump that
// has to stay on LONGER to deliver more is a pump with a LOWER ml/s. Every
// control on the calibration screen goes through here, so none of them can
// disagree about which way round that is.
enum class Dose { less, more };

float nudgeMlPerSecond(float mlPerSecond, Dose dose);

// The slider runs with the glass rather than with the rate: its right-hand end
// is the most liquid, which is the smallest ml/s. Mutual inverses.
constexpr int kDoseSliderMin = 2;
constexpr int kDoseSliderMax = 200;

int doseSlider(float mlPerSecond);

float doseSliderRate(int position);



float clampCupMm(float mm);

float nudgeCupMm(float mm, int steps);

int clampDebounce(int samples);

// A cup has to read nearer than the empty tray, with room for a threshold in
// between, or the sensor can never tell the two apart.
bool cupThresholdsSane(const bartender::CupSensorConfig& sensor);

// WPA2 refuses a passphrase outside 8..63 characters and an empty SSID cannot be
// joined at all. Both are caught here rather than by an access point that never
// comes back up once the new credentials are written.
bool wifiSsidValid(const std::string& ssid);

bool wifiPassphraseValid(const std::string& passphrase);

}  // namespace hmi
