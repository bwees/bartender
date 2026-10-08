#pragma once

#include <optional>
#include <string>

#include "domain.h"

namespace bartender {

class Filesystem {
 public:
  virtual ~Filesystem() = default;
  virtual std::optional<std::string> read(const std::string& path) const = 0;
  virtual bool write(const std::string& path, const std::string& content) const = 0;
  virtual bool rename(const std::string& from, const std::string& to) const = 0;
  virtual bool remove(const std::string& path) const = 0;
  virtual bool exists(const std::string& path) const = 0;
};

enum class LoadStatus { ok, missing, badVersion, parseError };

LoadStatus loadLibrary(const Filesystem& fs, const std::string& path, Library& out);
LoadStatus loadConfig(const Filesystem& fs, const std::string& path, Config& out);

std::optional<Library> parseLibrary(const std::string& content, int& versionOut);
std::optional<Config> parseConfig(const std::string& content, int& versionOut);

std::string serializeLibrary(const Library& library);
std::string serializeConfig(const Config& config);

bool saveLibrary(const Filesystem& fs, const std::string& path, const Library& library);
bool saveConfig(const Filesystem& fs, const std::string& path, const Config& config);

Config defaultConfig(const std::string& apSuffix);
Library defaultLibrary();

}
