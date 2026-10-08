#include "i2c_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t busMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  return mutex;
}

}  // namespace

I2cGuard::I2cGuard() { xSemaphoreTakeRecursive(busMutex(), portMAX_DELAY); }

I2cGuard::~I2cGuard() { xSemaphoreGiveRecursive(busMutex()); }
