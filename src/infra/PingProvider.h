#pragma once

#include "domain/IPingProvider.h"

// ICMP echo implementation on top of the ESP-IDF lwIP ping session API. One
// probe per call; the call blocks until the reply or the probe timeout.
class PingProvider : public IPingProvider {
 public:
  PingResult ping(const ServerConfig& server) override;
};
