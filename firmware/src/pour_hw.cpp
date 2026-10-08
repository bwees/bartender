#include "pour_hw.h"

#include <Wire.h>

#include "i2c_bus.h"

namespace {

constexpr uint8_t REG_MODE1 = 0x00;
constexpr uint8_t REG_MODE2 = 0x01;
constexpr uint8_t REG_LED0_ON_L = 0x06;
constexpr uint8_t REG_ALL_LED_ON_L = 0xFA;
constexpr uint8_t REG_PRE_SCALE = 0xFE;

constexpr uint8_t MODE1_SLEEP = 0x10;
constexpr uint8_t MODE1_AI = 0x20;
constexpr uint8_t MODE2_OUTDRV = 0x04;

// Prescale 30 == 200 Hz. Frequency is irrelevant for full-on/full-off switching,
// but it is written explicitly so the chip lands in a known state even when it
// survived an MCU reset with a different value latched.
constexpr uint8_t PRE_SCALE_200HZ = 30;

constexpr uint8_t FULL_ON_H = 0x10;
constexpr uint8_t FULL_OFF_H = 0x10;

// Pump slots are numbered 0-7 everywhere above this file; the wiring skips the
// middle of the PCA9685 header, so slot 4 lands on output 12, not output 8.
constexpr uint8_t PUMP_OUTPUT[] = {0, 1, 2, 3, 12, 13, 14, 15};
constexpr int PUMP_SLOTS = (int)(sizeof(PUMP_OUTPUT) / sizeof(PUMP_OUTPUT[0]));

}  // namespace

bool Pca9685::write(uint8_t reg, uint8_t value) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Pca9685::writeChannel(uint8_t reg, bool on) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write(0);
  Wire.write(on ? FULL_ON_H : 0);
  Wire.write(0);
  Wire.write(on ? 0 : FULL_OFF_H);
  return Wire.endTransmission() == 0;
}

// Ordering here is the safety-critical part of the pump path. An MCU reset does not
// reset the PCA9685, so a channel left full-on keeps a pump running across a reboot:
// the very first transaction must be the all-channels-off write, before MODE1, the
// prescale, or anything else. Everything after it happens with SLEEP set, so the
// oscillator is stopped while the configuration lands, and RESTART is never written
// — restarting would re-assert whatever PWM state the chip held before sleeping.
bool Pca9685::begin() {
  if (!allOff()) return false;
  if (!write(REG_MODE1, MODE1_SLEEP | MODE1_AI)) return false;
  if (!write(REG_MODE2, MODE2_OUTDRV)) return false;
  if (!write(REG_PRE_SCALE, PRE_SCALE_200HZ)) return false;
  if (!write(REG_MODE1, MODE1_AI)) return false;
  delayMicroseconds(500);
  return allOff();
}

bool Pca9685::set(int slot, bool on) {
  if (slot < 0 || slot >= PUMP_SLOTS) return false;
  return writeChannel(REG_LED0_ON_L + 4 * PUMP_OUTPUT[slot], on);
}

bool Pca9685::allOff() { return writeChannel(REG_ALL_LED_ON_L, false); }
