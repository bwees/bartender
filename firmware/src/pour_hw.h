#pragma once

#include <Arduino.h>
#include <pour.h>

class MillisClock : public pour::Clock {
 public:
  uint32_t millis() const override { return ::millis(); }
};

class Pca9685 : public pour::PumpDriver {
 public:
  explicit Pca9685(uint8_t address = 0x40) : address_(address) {}

  bool begin();
  // Takes a pump slot, not a PCA9685 output: the two differ, see PUMP_OUTPUT.
  bool set(int slot, bool on) override;
  bool allOff() override;

 private:
  bool write(uint8_t reg, uint8_t value);
  bool writeChannel(uint8_t reg, bool on);

  uint8_t address_;
};
