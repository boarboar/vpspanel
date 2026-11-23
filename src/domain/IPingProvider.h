#pragma once

#include "domain/PingResult.h"
#include "domain/ServerConfig.h"

// Port for ICMP reachability checks. Kept separate from IMetricsProvider so the
// ping poller never depends on SSH and can run on its own schedule.
class IPingProvider {
 public:
  virtual ~IPingProvider() = default;

  virtual PingResult ping(const ServerConfig& server) = 0;
};
