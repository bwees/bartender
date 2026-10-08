#include "tof.h"


#include <Wire.h>

#include "i2c_bus.h"

namespace {

constexpr uint8_t REG_SYSRANGE_START = 0x00;
constexpr uint8_t REG_SEQUENCE_CONFIG = 0x01;
constexpr uint8_t REG_FINAL_RANGE_MIN_COUNT_RATE = 0x44;
constexpr uint8_t REG_INTERRUPT_CONFIG_GPIO = 0x0A;
constexpr uint8_t REG_INTERRUPT_CLEAR = 0x0B;
constexpr uint8_t REG_INTERRUPT_STATUS = 0x13;
constexpr uint8_t REG_RANGE_MM = 0x1E;
constexpr uint8_t REG_MSRC_CONFIG_CONTROL = 0x60;
constexpr uint8_t REG_GPIO_HV_MUX_ACTIVE_HIGH = 0x84;
constexpr uint8_t REG_EXTSUP_HV = 0x89;
constexpr uint8_t REG_MODEL_ID = 0xC0;

constexpr uint8_t MODEL_ID_VL53L0X = 0xEE;
constexpr uint8_t INTERRUPT_NEW_SAMPLE_READY = 0x04;
constexpr uint8_t INTERRUPT_ANY = 0x07;

// 0.25 MCPS in 9.7 fixed point.
constexpr uint16_t FINAL_RANGE_MIN_COUNT_RATE = 32;

constexpr uint32_t SAMPLE_INTERVAL_MS = 50;
constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 100;
constexpr uint32_t CALIBRATION_TIMEOUT_MS = 50;
constexpr uint32_t PROBE_INTERVAL_MS = 2000;
constexpr int FAILURES_BEFORE_UNWIRED = 5;

}  // namespace

bool Tof::write8(uint8_t reg, uint8_t value) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Tof::write16(uint8_t reg, uint16_t value) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission() == 0;
}

bool Tof::read8(uint8_t reg, uint8_t& value) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address_, (size_t)1) != 1) return false;
  value = (uint8_t)Wire.read();
  return true;
}

bool Tof::read16(uint8_t reg, uint16_t& value) {
  I2cGuard guard;
  Wire.beginTransmission(address_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address_, (size_t)2) != 2) return false;
  value = (uint16_t)Wire.read() << 8;
  value |= (uint8_t)Wire.read();
  return true;
}

bool Tof::setBits(uint8_t reg, uint8_t set, uint8_t clear) {
  uint8_t value = 0;
  if (!read8(reg, value)) return false;
  return write8(reg, (uint8_t)((value & ~clear) | set));
}

bool Tof::probe() {
  uint8_t id = 0;
  return read8(REG_MODEL_ID, id) && id == MODEL_ID_VL53L0X;
}

// Trimmed against ST's reference sequence: the NVM-loaded SPAD map and the factory
// tuning blob are both left as the part powers up. That costs absolute accuracy the
// bar does not need - a glass at ~45mm against a tray at ~95mm is a wide margin -
// and it keeps this driver small enough to hand-roll rather than take a dependency.
bool Tof::configure() {
  I2cGuard guard;
  if (!setBits(REG_EXTSUP_HV, 0x01, 0x00)) return false;
  if (!write8(0x88, 0x00)) return false;

  if (!write8(0x80, 0x01)) return false;
  if (!write8(0xFF, 0x01)) return false;
  if (!write8(0x00, 0x00)) return false;
  if (!read8(0x91, stopVariable_)) return false;
  if (!write8(0x00, 0x01)) return false;
  if (!write8(0xFF, 0x00)) return false;
  if (!write8(0x80, 0x00)) return false;

  if (!setBits(REG_MSRC_CONFIG_CONTROL, 0x12, 0x00)) return false;
  if (!write16(REG_FINAL_RANGE_MIN_COUNT_RATE, FINAL_RANGE_MIN_COUNT_RATE)) return false;

  if (!write8(REG_INTERRUPT_CONFIG_GPIO, INTERRUPT_NEW_SAMPLE_READY)) return false;
  if (!setBits(REG_GPIO_HV_MUX_ACTIVE_HIGH, 0x00, 0x10)) return false;
  if (!write8(REG_INTERRUPT_CLEAR, 0x01)) return false;

  if (!write8(REG_SEQUENCE_CONFIG, 0x01) || !calibrateRef(0x40)) return false;
  if (!write8(REG_SEQUENCE_CONFIG, 0x02) || !calibrateRef(0x00)) return false;
  return write8(REG_SEQUENCE_CONFIG, 0xE8);
}

bool Tof::awaitSample(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  for (;;) {
    uint8_t status = 0;
    if (!read8(REG_INTERRUPT_STATUS, status)) return false;
    if ((status & INTERRUPT_ANY) != 0) return true;
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(1);
  }
}

bool Tof::calibrateRef(uint8_t vhvInitByte) {
  if (!write8(REG_SYSRANGE_START, (uint8_t)(0x01 | vhvInitByte))) return false;
  if (!awaitSample(CALIBRATION_TIMEOUT_MS)) return false;
  if (!write8(REG_INTERRUPT_CLEAR, 0x01)) return false;
  return write8(REG_SYSRANGE_START, 0x00);
}

bool Tof::begin() {
  sensorPresent_ = probe() && configure();
  phase_ = Phase::idle;
  consecutiveFailures_ = 0;
  if (!sensorPresent_) lastMm_ = -1.0f;
  return sensorPresent_;
}

bool Tof::startMeasurement() {
  I2cGuard guard;
  if (!write8(0x80, 0x01)) return false;
  if (!write8(0xFF, 0x01)) return false;
  if (!write8(0x00, 0x00)) return false;
  if (!write8(0x91, stopVariable_)) return false;
  if (!write8(0x00, 0x01)) return false;
  if (!write8(0xFF, 0x00)) return false;
  if (!write8(0x80, 0x00)) return false;
  return write8(REG_SYSRANGE_START, 0x01);
}

void Tof::recordSample(uint32_t nowMs, uint16_t mm) {
  phase_ = Phase::idle;
  lastSampleMs_ = nowMs;
  consecutiveFailures_ = 0;
  lastMm_ = (float)mm;
  detector_.update(cup::ranged(lastMm_));
}

void Tof::recordFailure(uint32_t nowMs) {
  phase_ = Phase::idle;
  lastSampleMs_ = nowMs;
  lastMm_ = -1.0f;
  detector_.update(cup::failed());
  if (++consecutiveFailures_ < FAILURES_BEFORE_UNWIRED) return;
  sensorPresent_ = false;
  lastProbeMs_ = nowMs;
}

void Tof::poll(uint32_t nowMs, const bartender::CupSensorConfig& config) {
  detector_.configure(config);

  if (!sensorPresent_) {
    if (nowMs - lastProbeMs_ < PROBE_INTERVAL_MS) return;
    lastProbeMs_ = nowMs;
    detector_.update(cup::failed());
    begin();
    return;
  }

  if (phase_ == Phase::idle) {
    if (nowMs - lastSampleMs_ < SAMPLE_INTERVAL_MS) return;
    if (!startMeasurement()) {
      recordFailure(nowMs);
      return;
    }
    phase_ = Phase::measuring;
    startedMs_ = nowMs;
    return;
  }

  uint8_t status = 0;
  if (!read8(REG_INTERRUPT_STATUS, status)) {
    recordFailure(nowMs);
    return;
  }
  if ((status & INTERRUPT_ANY) == 0) {
    if (nowMs - startedMs_ >= MEASUREMENT_TIMEOUT_MS) {
      recordFailure(nowMs);
    }
    return;
  }

  uint16_t mm = 0;
  const bool read = read16(REG_RANGE_MM, mm);
  write8(REG_INTERRUPT_CLEAR, 0x01);
  if (read) {
    recordSample(nowMs, mm);
  } else {
    recordFailure(nowMs);
  }
}
