#include "theme.h"

namespace hmi {
namespace {

bool nibble(char c, uint32_t& out) {
  if (c >= '0' && c <= '9') {
    out = (uint32_t)(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = (uint32_t)(c - 'a') + 10;
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = (uint32_t)(c - 'A') + 10;
    return true;
  }
  return false;
}

}  // namespace

// lv_color_hex truncates the low bits rather than replicating the high ones, so
// this must truncate too or the check disagrees with what the panel is sent.
uint32_t quantise565(uint32_t rgb) {
  return (rgb & 0xF80000) | (rgb & 0x00FC00) | (rgb & 0x0000F8);
}

bool parseHex(const std::string& text, uint32_t& out) {
  size_t at = (!text.empty() && text[0] == '#') ? 1 : 0;
  if (text.size() - at != 6) return false;
  uint32_t value = 0;
  for (size_t i = at; i < text.size(); i++) {
    uint32_t digit = 0;
    if (!nibble(text[i], digit)) return false;
    value = (value << 4) | digit;
  }
  out = value;
  return true;
}

uint32_t hueFor(const std::string& catalogueColour, int slotIndex) {
  if (slotIndex >= 0) return kSlotHues[slotIndex % kSlotHueCount];
  uint32_t parsed = 0;
  if (parseHex(catalogueColour, parsed)) return quantise565(parsed);
  return colour::kLine;
}

}  // namespace hmi
