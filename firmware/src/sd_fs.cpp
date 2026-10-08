#include "sd_fs.h"

#include <SPI.h>

namespace {

constexpr int PIN_SD_SCK = 5;
constexpr int PIN_SD_MISO = 4;
constexpr int PIN_SD_MOSI = 6;
constexpr uint32_t SD_FREQ_HZ = 25000000;

SemaphoreHandle_t busMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  return mutex;
}

}  // namespace

SdGuard::SdGuard() { xSemaphoreTakeRecursive(busMutex(), portMAX_DELAY); }

SdGuard::~SdGuard() { xSemaphoreGiveRecursive(busMutex()); }

bool SdFs::mount() {
  SdGuard guard;
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI);
  return SD.begin(0, SPI, SD_FREQ_HZ);
}

std::optional<std::string> SdFs::read(const std::string& path) const {
  SdGuard guard;
  File file = SD.open(path.c_str(), FILE_READ);
  if (!file || file.isDirectory()) return std::nullopt;
  std::string out(file.size(), '\0');
  const size_t got = file.read(reinterpret_cast<uint8_t*>(out.data()), out.size());
  file.close();
  if (got != out.size()) return std::nullopt;
  return out;
}

bool SdFs::write(const std::string& path, const std::string& content) const {
  SdGuard guard;
  File file = SD.open(path.c_str(), FILE_WRITE);
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(content.data()),
                             content.size()) == static_cast<size_t>(content.size());
  file.close();
  return ok;
}

bool SdFs::rename(const std::string& from, const std::string& to) const {
  SdGuard guard;
  return SD.rename(from.c_str(), to.c_str());
}

bool SdFs::remove(const std::string& path) const {
  SdGuard guard;
  return SD.remove(path.c_str());
}

bool SdFs::exists(const std::string& path) const {
  SdGuard guard;
  return SD.exists(path.c_str());
}
