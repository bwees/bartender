#include "gt911.h"

#include <Wire.h>

#include "i2c_bus.h"

namespace gt911 {
namespace {

constexpr uint8_t kAddr = 0x5D;
constexpr uint16_t kRegProductId = 0x8140;
constexpr uint16_t kRegStatus = 0x814E;
// Point 1 record: 0x8150 X low, 0x8151 X high, 0x8152 Y low, 0x8153 Y high. The
// track ID is at 0x8157, not ahead of the coordinates — reading one byte late
// silently returns Y as X and the contact size as Y.
constexpr uint16_t kRegPoint1 = 0x8150;

bool detected = false;

bool readRegs(uint16_t reg, uint8_t* out, size_t len) {
  I2cGuard guard;
  Wire.beginTransmission(kAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)kAddr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

bool writeReg(uint16_t reg, uint8_t value) {
  I2cGuard guard;
  Wire.beginTransmission(kAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
  uint8_t id[4] = {};
  detected = readRegs(kRegProductId, id, sizeof(id));
  return detected;
}

bool present() { return detected; }

bool read(int32_t& x, int32_t& y) {
  if (!detected) return false;

  uint8_t status = 0;
  if (!readRegs(kRegStatus, &status, 1)) return false;
  if ((status & 0x80) == 0) return false;

  const uint8_t touches = status & 0x0F;
  uint8_t point[4] = {};
  const bool ok = touches >= 1 && readRegs(kRegPoint1, point, sizeof(point));

  // The buffer flag must be cleared or the controller stops refreshing it.
  writeReg(kRegStatus, 0);
  if (!ok) return false;

  x = (int32_t)((uint16_t)point[0] | ((uint16_t)point[1] << 8));
  y = (int32_t)((uint16_t)point[2] | ((uint16_t)point[3] << 8));
  return true;
}

}  // namespace gt911
