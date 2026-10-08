#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain.h"

namespace hmi {

enum class Missing { none, notAssigned, slotEmpty };

struct SheetRow {
  std::string ingredientId;
  std::string name;
  float ml = 0.0f;
  bool present = false;
  uint32_t hue = 0;
};

// Holds the recipe by value, not by pointer. The portal rewrites library.recipes
// from the HTTP task, which would dangle any pointer the menu kept between
// rebuilds; a copy costs a few hundred bytes and cannot be invalidated.
struct MenuEntry {
  bartender::Recipe recipe;
  bool available = false;
  // "NEEDS RUM" / "NEEDS 2 BOTTLES", empty when available. Reads at three metres.
  std::string badge;
  // "Needs rum — not in any slot", empty when available.
  std::string reason;
  char initial = '?';
};

const bartender::Ingredient* findIngredient(const bartender::Library& library,
                                            const std::string& id);

// The slot an ingredient is loaded in, ignoring the empty flag, or -1.
int slotOf(const std::vector<bartender::Slot>& slots, const std::string& ingredientId);

Missing missingKind(const std::vector<bartender::Slot>& slots, const std::string& ingredientId);

std::vector<SheetRow> sheetRows(const bartender::Recipe& recipe,
                                const bartender::Library& library,
                                const std::vector<bartender::Slot>& slots);

// Available first, then unavailable; each group ordered by name, case-insensitively.
std::vector<MenuEntry> buildMenu(const bartender::Library& library,
                                 const std::vector<bartender::Slot>& slots);

std::vector<int> pourableIndices(const std::vector<MenuEntry>& menu);

std::string upperCase(const std::string& text);

}  // namespace hmi
