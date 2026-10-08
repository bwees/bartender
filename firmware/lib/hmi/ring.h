#pragma once

#include <cstdint>
#include <vector>

#include "domain.h"
#include "screen.h"

namespace hmi {

constexpr uint8_t kFullLevel = 255;

enum class RingEffect { rainbow, solid, pulse };

struct RingLook {
  RingEffect effect;
  // Ignored by rainbow, which carries a colour per pixel instead.
  uint32_t rgb;
  // Rainbow: one full turn. Pulse: one dark-bright-dark swing. Unused by solid.
  uint32_t periodMs;
};

// What the ring is saying about the machine right now.
RingLook ringLook(Screen screen);

// A breathe, not a blink: dark to bright and back over one period, on a cosine so
// neither end of the swing has a corner in it.
uint8_t breathLevel(uint32_t nowMs, uint32_t periodMs);

// A breathe that never reaches black, so a pulsing ring still lights the cup.
// Linear in the cosine rather than gamma-curved: the curve that flatters a slow
// breathe holds a short period near black and then snaps, which reads as a strobe.
uint8_t pulseLevel(uint32_t nowMs, uint32_t periodMs);

// The whole spectrum laid once around the ring, turning one revolution per period.
uint32_t rainbowPixel(int index, int pixels, uint32_t nowMs, uint32_t periodMs);

// Position 0-255 around the colour wheel, red at 0.
uint32_t wheel(uint8_t position);

// Per-channel, so a dimmed hue keeps its hue instead of sliding toward grey.
uint32_t dim(uint32_t rgb, uint8_t level);

// The owner's master brightness against an effect's own level.
uint8_t ringLevel(uint8_t effectLevel, int masterBrightness);

}  // namespace hmi
