#pragma once

#include <cstdint>

// The WS2812 ring around the cup platform. Data is GPIO43, which is also UART0 TX:
// once begin() claims the pin, the serial console goes with it. Power comes off
// `J9` pins 5 and 6.
namespace led {

bool begin(int pixels);

// One colour across the whole ring, pushed now. Black is off. A no-op until
// begin() has claimed the pin, so a UI tick that starts early costs nothing.
void fill(uint32_t rgb);

// A whole frame, one 0xRRGGBB per pixel, clockwise from the ring's first LED.
// Ignored unless count matches what begin() claimed.
void show(const uint32_t* pixels, int count);

}  // namespace led
