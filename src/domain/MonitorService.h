#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <map>
#include <vector>

#include "domain/AppSettings.h"
#include "domain/IMetricsProvider.h"
#include "domain/ServerConfig.h"
#include "domain/ServerMetrics.h"
#include "domain/ServerRegistry.h"

// Business logic: decides when servers must be polled, performs the polls and
// keeps the latest result per server. It knows nothing about Serial or HTTP.
class MonitorService {
 public:
  struct Snapshot {
    ServerConfig server;
    ServerMetrics metrics;
  };

  // Presentation layers subscribe here to receive poll events.
  class Listener {
   public:
    virtual ~Listener() = default;
    virtual void onCycleStarted(size_t serverCount) { (void)serverCount; }
    virtual void onPollStarted(const ServerConfig& server) { (void)server; }
    virtual void onPollFinished(const ServerConfig& server, const ServerMetrics& metrics) {
      (void)server;
      (void)metrics;
    }
    virtual void onCycleFinished(size_t polledCount, uint32_t durationMs) {
      (void)polledCount;
      (void)durationMs;
    }
  };

  MonitorService(ServerRegistry& registry, IMetricsProvider& provider);

  void begin();
  void setListener(Listener* listener);

  // True when the configured interval elapsed or the web UI asked for an
  // immediate poll (the request flag is consumed by this call).
  bool pollDue();

  void pollAll();
  void requestPoll();

  bool isPolling() const;
  uint32_t lastCycleAtMs() const;

  std::vector<Snapshot> snapshots() const;

 private:
  struct Guard {
    explicit Guard(SemaphoreHandle_t mutex) : mutex_(mutex) {
      if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~Guard() {
      if (mutex_) xSemaphoreGive(mutex_);
    }
    SemaphoreHandle_t mutex_;
  };

  ServerRegistry& registry_;
  IMetricsProvider& provider_;
  Listener* listener_ = nullptr;

  mutable SemaphoreHandle_t mutex_ = nullptr;
  std::map<String, ServerMetrics> metrics_;

  std::atomic<bool> immediateRequest_{false};
  std::atomic<bool> polling_{false};
  uint32_t lastCycleAtMs_ = 0;
};
