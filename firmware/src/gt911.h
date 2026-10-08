#pragma once

#include <stdint.h>

// LovyanGFX's Touch_GT911 programs the I2C0 peripheral registers directly rather
// than going through Arduino Wire, and never hands it back — after gfx.init() a
// Wire bus scan finds nothing at all, which silently kills the pump driver and the
// ToF sensor. So the panel keeps LovyanGFX and the touch controller does not.
namespace gt911 {

bool begin();
bool present();
// Returns true and fills x/y while a finger is down.
bool read(int32_t& x, int32_t& y);

}  // namespace gt911
