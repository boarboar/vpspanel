#pragma once

#include "domain/IMetricsProvider.h"

// Fetches metrics from a Linux host over SSH using LibSSH-ESP32.
class SshMetricsProvider : public IMetricsProvider {
 public:
  ServerMetrics fetch(const ServerConfig& server, uint16_t timeoutSec) override;
};
