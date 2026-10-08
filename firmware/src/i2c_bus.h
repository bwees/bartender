#pragma once

// The PCA9685, the GT911 and the companion MCU share one bus, and the pour
// scheduler must be able to cut the pumps without waiting on the UI task. That
// rules out the cheaper "keep all I2C on one task" arrangement, so every Wire
// transaction takes this instead.
class I2cGuard {
 public:
  I2cGuard();
  ~I2cGuard();
  I2cGuard(const I2cGuard&) = delete;
  I2cGuard& operator=(const I2cGuard&) = delete;
};
