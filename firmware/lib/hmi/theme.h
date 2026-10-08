#pragma once

#include <cstdint>
#include <string>

namespace hmi {

namespace colour {

constexpr uint32_t kBase = 0x101010;
constexpr uint32_t kRaised = 0x202020;
constexpr uint32_t kPressed = 0x303030;
constexpr uint32_t kDisabled = 0x181818;
constexpr uint32_t kLine = 0x484848;
constexpr uint32_t kRule = 0x303030;
constexpr uint32_t kTrack = 0x242424;
constexpr uint32_t kTextPrimary = 0xF8F8F8;
constexpr uint32_t kTextSecondary = 0xC0C0C0;
constexpr uint32_t kTextDim = 0x909090;
constexpr uint32_t kAccent = 0x38A8F8;
constexpr uint32_t kAccentBed = 0x082840;
constexpr uint32_t kDanger = 0xF84828;
constexpr uint32_t kDangerBed = 0x280C08;
constexpr uint32_t kDangerRaised = 0x401410;
constexpr uint32_t kUnavailable = 0xF0C430;

}  // namespace colour

// The eight bottle hues, indexed by slot. None sits in the accent's blue lane, so
// "this is a bottle" and "this is a button" never trade places.
constexpr uint32_t kSlotHues[8] = {
    0xB8A0F0, 0xA8B8C8, 0xC88C40, 0xC8D058, 0x78D048, 0xF0C430, 0xE04868, 0x48C8C0,
};
constexpr int kSlotHueCount = 8;

// Snaps a colour onto an exact RGB565 word so a fill cannot dither at its edges.
uint32_t quantise565(uint32_t rgb);

// Parses "#RRGGBB" or "RRGGBB". Returns false and leaves out untouched otherwise.
bool parseHex(const std::string& text, uint32_t& out);

// A loaded slot always takes its fixed hue: the eight are chosen to survive RGB565
// exactly and to stay out of the accent's blue lane, which an arbitrary catalogue
// colour cannot promise. The catalogue colour is the fallback for an ingredient
// that is in no slot, and a neutral when even that will not parse.
uint32_t hueFor(const std::string& catalogueColour, int slotIndex);

}  // namespace hmi
