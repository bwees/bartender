#include "menu.h"

#include <algorithm>
#include <cctype>

#include "theme.h"

namespace hmi {
namespace {

int compareNames(const std::string& a, const std::string& b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    int ca = std::tolower((unsigned char)a[i]);
    int cb = std::tolower((unsigned char)b[i]);
    if (ca != cb) return ca < cb ? -1 : 1;
  }
  if (a.size() == b.size()) return 0;
  return a.size() < b.size() ? -1 : 1;
}

std::string lowerFirst(const std::string& text) {
  if (text.empty()) return text;
  std::string out = text;
  out[0] = (char)std::tolower((unsigned char)out[0]);
  return out;
}

}  // namespace

std::string upperCase(const std::string& text) {
  std::string out = text;
  for (auto& c : out) c = (char)std::toupper((unsigned char)c);
  return out;
}

const bartender::Ingredient* findIngredient(const bartender::Library& library,
                                            const std::string& id) {
  for (const auto& ingredient : library.ingredients) {
    if (ingredient.id == id) return &ingredient;
  }
  return nullptr;
}

int slotOf(const std::vector<bartender::Slot>& slots, const std::string& ingredientId) {
  for (const auto& slot : slots) {
    if (slot.ingredientId && *slot.ingredientId == ingredientId) return slot.index;
  }
  return -1;
}

Missing missingKind(const std::vector<bartender::Slot>& slots, const std::string& ingredientId) {
  bool assigned = false;
  for (const auto& slot : slots) {
    if (!slot.ingredientId || *slot.ingredientId != ingredientId) continue;
    assigned = true;
    if (!slot.empty) return Missing::none;
  }
  return assigned ? Missing::slotEmpty : Missing::notAssigned;
}

std::vector<SheetRow> sheetRows(const bartender::Recipe& recipe,
                                const bartender::Library& library,
                                const std::vector<bartender::Slot>& slots) {
  std::vector<SheetRow> rows;
  rows.reserve(recipe.pours.size());
  for (const auto& item : recipe.pours) {
    SheetRow row;
    const auto* found = findIngredient(library, item.ingredientId);
    row.ingredientId = item.ingredientId;
    row.name = found ? found->name : item.ingredientId;
    row.ml = item.ml;
    row.present = missingKind(slots, item.ingredientId) == Missing::none;
    // A missing ingredient loses its hue so "one of these three is the problem"
    // is legible before a word is read.
    row.hue = row.present ? hueFor(found ? found->colour : std::string(),
                                   slotOf(slots, item.ingredientId))
                          : colour::kPressed;
    rows.push_back(row);
  }
  return rows;
}

std::vector<MenuEntry> buildMenu(const bartender::Library& library,
                                 const std::vector<bartender::Slot>& slots) {
  std::vector<MenuEntry> menu;
  menu.reserve(library.recipes.size());

  for (const auto& recipe : library.recipes) {
    MenuEntry entry;
    entry.recipe = recipe;
    entry.available = bartender::servable(recipe, slots);
    if (!recipe.name.empty()) entry.initial = (char)std::toupper((unsigned char)recipe.name[0]);

    if (!entry.available) {
      std::vector<std::string> names;
      Missing firstKind = Missing::none;
      int firstSlot = -1;
      for (const auto& item : recipe.pours) {
        Missing kind = missingKind(slots, item.ingredientId);
        if (kind == Missing::none) continue;
        const auto* ingredient = findIngredient(library, item.ingredientId);
        names.push_back(ingredient ? ingredient->name : item.ingredientId);
        if (firstKind == Missing::none) {
          firstKind = kind;
          firstSlot = slotOf(slots, item.ingredientId);
        }
      }

      if (names.size() == 1) {
        entry.badge = "NEEDS " + upperCase(names[0]);
        entry.reason = "Needs " + lowerFirst(names[0]);
        if (firstKind == Missing::slotEmpty) {
          entry.reason += " - slot " + std::to_string(firstSlot + 1) + " is empty";
        } else {
          entry.reason += " - not in any slot";
        }
      } else if (names.size() > 1) {
        entry.badge = "NEEDS " + std::to_string(names.size()) + " BOTTLES";
        entry.reason = "Needs " + lowerFirst(names[0]);
        for (size_t i = 1; i + 1 < names.size(); i++) entry.reason += ", " + lowerFirst(names[i]);
        entry.reason += " and " + lowerFirst(names.back());
      }
    }
    menu.push_back(entry);
  }

  std::stable_sort(menu.begin(), menu.end(), [](const MenuEntry& a, const MenuEntry& b) {
    if (a.available != b.available) return a.available;
    return compareNames(a.recipe.name, b.recipe.name) < 0;
  });
  return menu;
}

std::vector<int> pourableIndices(const std::vector<MenuEntry>& menu) {
  std::vector<int> out;
  for (size_t i = 0; i < menu.size(); i++) {
    if (menu[i].available) out.push_back((int)i);
  }
  return out;
}

}  // namespace hmi
