#include "ring.h"

#include <cmath>

namespace hmi {
namespace {

// White, because the ring's other job on every non-idle screen is lighting the cup.
constexpr uint32_t kCupRgb = 0xFFFFFF;
constexpr uint32_t kFaultRgb = 0xFF0000;

constexpr uint32_t kRainbowTurnMs = 20000;
constexpr uint32_t kPourPulseMs = 3000;
constexpr uint32_t kPausedPulseMs = 800;
constexpr uint32_t kFaultPulseMs = 1500;

constexpr uint8_t kPulseFloor = 40;
constexpr uint32_t kWheelSize = 256;
constexpr uint8_t kWheelArc = 85;

uint8_t channel(uint32_t rgb, int shift) { return (uint8_t)((rgb >> shift) & 0xFF); }

// 0 at the start of a period, 1 at the halfway point, 0 again at the end.
float swingAt(uint32_t nowMs, uint32_t periodMs) {
  if (periodMs == 0) return 0.0f;
  const float phase = (float)(nowMs % periodMs) / (float)periodMs;
  return 0.5f - 0.5f * cosf(2.0f * (float)M_PI * phase);
}

}  // namespace

RingLook ringLook(Screen screen) {
  switch (screen) {
    case Screen::attract: return {RingEffect::rainbow, 0, kRainbowTurnMs};
    case Screen::fault: return {RingEffect::pulse, kFaultRgb, kFaultPulseMs};
    case Screen::pouring: return {RingEffect::pulse, kCupRgb, kPourPulseMs};
    case Screen::paused: return {RingEffect::pulse, kCupRgb, kPausedPulseMs};
    default: return {RingEffect::solid, kCupRgb, 0};
  }
}

uint8_t breathLevel(uint32_t nowMs, uint32_t periodMs) {
  const float swing = swingAt(nowMs, periodMs);
  // Squared, because a WS2812's duty cycle is linear and the eye is not.
  return (uint8_t)(swing * swing * kFullLevel + 0.5f);
}

uint8_t pulseLevel(uint32_t nowMs, uint32_t periodMs) {
  const float swing = swingAt(nowMs, periodMs);
  return (uint8_t)(kPulseFloor + swing * (float)(kFullLevel - kPulseFloor) + 0.5f);
}

uint32_t wheel(uint8_t position) {
  if (position < kWheelArc) {
    return ((uint32_t)(kFullLevel - position * 3) << 16) | ((uint32_t)(position * 3) << 8);
  }
  if (position < kWheelArc * 2) {
    position -= kWheelArc;
    return ((uint32_t)(kFullLevel - position * 3) << 8) | (uint32_t)(position * 3);
  }
  position -= kWheelArc * 2;
  return ((uint32_t)(position * 3) << 16) | (uint32_t)(kFullLevel - position * 3);
}

uint32_t rainbowPixel(int index, int pixels, uint32_t nowMs, uint32_t periodMs) {
  if (pixels <= 0) return 0;
  const uint32_t spin = periodMs == 0 ? 0 : (nowMs % periodMs) * kWheelSize / periodMs;
  return wheel((uint8_t)(((uint32_t)index * kWheelSize / (uint32_t)pixels + spin) % kWheelSize));
}

uint32_t dim(uint32_t rgb, uint8_t level) {
  const uint32_t red = channel(rgb, 16) * level / kFullLevel;
  const uint32_t green = channel(rgb, 8) * level / kFullLevel;
  const uint32_t blue = channel(rgb, 0) * level / kFullLevel;
  return (red << 16) | (green << 8) | blue;
}

uint8_t ringLevel(uint8_t effectLevel, int masterBrightness) {
  if (masterBrightness <= 0) return 0;
  const int master = masterBrightness > kFullLevel ? kFullLevel : masterBrightness;
  return (uint8_t)(effectLevel * master / kFullLevel);
}

}  // namespace hmi
