#include "state_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

namespace {

SemaphoreHandle_t stateMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  return mutex;
}

std::atomic<uint32_t> revision{0};

}  // namespace

StateGuard::StateGuard() { xSemaphoreTakeRecursive(stateMutex(), portMAX_DELAY); }

StateGuard::~StateGuard() { xSemaphoreGiveRecursive(stateMutex()); }

uint32_t stateRevision() { return revision.load(std::memory_order_acquire); }

void bumpStateRevision() { revision.fetch_add(1, std::memory_order_release); }
