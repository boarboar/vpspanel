#pragma once

#include <vector>

#include "domain/AppSettings.h"
#include "domain/ServerConfig.h"

// Persistence port for the configuration. Implemented by the infrastructure
// layer (NVS / flash in this project), which keeps the domain storage-agnostic.
class ServerStore {
 public:
  virtual ~ServerStore() = default;

  // Returns false when no saved configuration exists or it cannot be read.
  virtual bool load(AppSettings& settings, std::vector<ServerConfig>& servers) = 0;
  virtual bool save(const AppSettings& settings, const std::vector<ServerConfig>& servers) = 0;
};
