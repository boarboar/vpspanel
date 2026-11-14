#pragma once

#include "domain/ServerConfig.h"
#include "domain/ServerMetrics.h"

// Port for the actual metrics transport. The domain service only knows that
// something can fetch metrics for a server; it does not know about SSH.
class IMetricsProvider {
 public:
  virtual ~IMetricsProvider() = default;

  virtual ServerMetrics fetch(const ServerConfig& server, uint16_t timeoutSec) = 0;
};
