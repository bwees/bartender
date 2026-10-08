#pragma once

#include <functional>

#include "domain.h"
#include "storage.h"

struct PortalState {
  bartender::Filesystem* fs = nullptr;
  bartender::Library* library = nullptr;
  bartender::Config* config = nullptr;
  std::function<bool()> pouring;
};

bool portalStart(const PortalState& state);
