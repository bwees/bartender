#pragma once

#include <Arduino.h>
#include <cup.h>

#include "domain.h"

// VL53L0X on the shared I2C bus at 0x29. Every transaction takes I2cGuard: the
// GT911 is driven by LovyanGFX straight through the I2C0 peripheral registers
// rather than through Wire, so two drivers share one peripheral.
class Tof {
 public:
  explicit Tof(uint8_t address = 0x29) : address_(address) {}

  bool begin();

  // Never waits on the sensor. One call starts a single-shot measurement, a later
  // call collects it, and an unplugged sensor costs one NAK every few seconds.
  void poll(uint32_t nowMs, const bartender::CupSensorConfig& config);

  // False means nothing answered at 0x29, so the UI can say "no sensor wired"
  // instead of quoting a distance nothing measured.
  bool sensorPresent() const { return sensorPresent_; }

  bool cupPresent() const { return detector_.cupPresent(); }

  cup::Presence presence() const { return detector_.presence(); }

  // The raw last measurement for the settings screen's live reading. Negative when
  // the last attempt failed or none has completed yet.
  float lastMm() const { return lastMm_; }

 private:
  enum class Phase { idle, measuring };

  bool write8(uint8_t reg, uint8_t value);
  bool write16(uint8_t reg, uint16_t value);
  bool read8(uint8_t reg, uint8_t& value);
  bool read16(uint8_t reg, uint16_t& value);
  bool setBits(uint8_t reg, uint8_t set, uint8_t clear);
  bool probe();
  bool configure();
  bool calibrateRef(uint8_t vhvInitByte);
  bool awaitSample(uint32_t timeoutMs);
  bool startMeasurement();
  void recordSample(uint32_t nowMs, uint16_t mm);
  void recordFailure(uint32_t nowMs);

  cup::Detector detector_;
  uint8_t address_;
  uint8_t stopVariable_ = 0;
  bool sensorPresent_ = false;
  Phase phase_ = Phase::idle;
  uint32_t startedMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastProbeMs_ = 0;
  int consecutiveFailures_ = 0;
  float lastMm_ = -1.0f;
};
