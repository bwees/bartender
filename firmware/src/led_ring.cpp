#include "led_ring.h"

#include <Arduino.h>

#include <algorithm>
#include <vector>

namespace led {
namespace {

constexpr int kDataPin = 43;
constexpr int kMaxPixels = 256;
constexpr size_t kBitsPerPixel = 24;

// 100 ns per tick, which divides every WS2812 interval below into whole ticks.
constexpr uint32_t kTickHz = 10 * 1000 * 1000;
constexpr uint16_t kOneHigh = 8;
constexpr uint16_t kOneLow = 4;
constexpr uint16_t kZeroHigh = 4;
constexpr uint16_t kZeroLow = 8;
// Frames go out on a 33 ms timer, so the gap between them is the >50 us latch.
constexpr uint32_t kWriteTimeoutMs = 20;

std::vector<rmt_data_t> frame;
std::vector<uint32_t> uniform;
int pixelCount = 0;
// Callers tick at a fixed rate whether or not the effect moved, and re-clocking
// the same frame down the wire buys nothing.
std::vector<uint32_t> shown;
bool everShown = false;

void encode(rmt_data_t* symbols, uint8_t byte) {
  for (int bit = 7; bit >= 0; bit--) {
    const bool one = (byte >> bit) & 1;
    symbols->level0 = 1;
    symbols->duration0 = one ? kOneHigh : kZeroHigh;
    symbols->level1 = 0;
    symbols->duration1 = one ? kOneLow : kZeroLow;
    symbols++;
  }
}

}  // namespace

bool begin(int pixels) {
  if (pixels <= 0 || pixels > kMaxPixels) return false;
  if (!rmtInit(kDataPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, kTickHz)) return false;
  // Idle low. A line left high is a WS2812 waiting for data that never comes.
  rmtSetEOT(kDataPin, 0);

  pixelCount = pixels;
  frame.resize((size_t)pixels * kBitsPerPixel);
  uniform.resize((size_t)pixels);
  shown.resize((size_t)pixels);
  fill(0);
  return true;
}

void show(const uint32_t* pixels, int count) {
  if (pixelCount == 0 || count != pixelCount) return;
  if (everShown && std::equal(pixels, pixels + count, shown.begin())) return;
  shown.assign(pixels, pixels + count);
  everShown = true;

  for (int pixel = 0; pixel < count; pixel++) {
    const uint32_t rgb = pixels[pixel];
    rmt_data_t* at = frame.data() + (size_t)pixel * kBitsPerPixel;
    encode(at, (rgb >> 8) & 0xFF);
    encode(at + 8, (rgb >> 16) & 0xFF);
    encode(at + 16, rgb & 0xFF);
  }
  rmtWrite(kDataPin, frame.data(), frame.size(), kWriteTimeoutMs);
}

void fill(uint32_t rgb) {
  if (pixelCount == 0) return;
  std::fill(uniform.begin(), uniform.end(), rgb);
  show(uniform.data(), pixelCount);
}

}  // namespace led
