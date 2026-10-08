#pragma once

#include <SD.h>
#include <storage.h>

// The SD SPI bus has CS tied low so the card is permanently selected, and LVGL
// image decoding contends with the portal for it. Every SD touch — inside this
// adapter or in the portal's chunked streaming — is serialised by SdGuard.
class SdGuard {
 public:
  SdGuard();
  ~SdGuard();
  SdGuard(const SdGuard&) = delete;
  SdGuard& operator=(const SdGuard&) = delete;
};

class SdFs : public bartender::Filesystem {
 public:
  bool mount();
  std::optional<std::string> read(const std::string& path) const override;
  bool write(const std::string& path, const std::string& content) const override;
  bool rename(const std::string& from, const std::string& to) const override;
  bool remove(const std::string& path) const override;
  bool exists(const std::string& path) const override;
};
