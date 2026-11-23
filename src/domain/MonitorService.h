#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <map>
#include <vector>

#include "domain/AppSettings.h"
#include "domain/IMetricsProvider.h"
#include "domain/IPingProvider.h"
#include "domain/PingResult.h"
#include "domain/ServerConfig.h"
#include "domain/ServerMetrics.h"
#include "domain/ServerRegistry.h"

// Business logic: decides when servers must be polled, performs the polls and
// keeps the latest result per server. The SSH metrics and the ICMP ping checks
// have independent schedules, state and result maps; they only share the server
// list. It knows nothing about Serial, HTTP or lwIP.
class MonitorService {
 public:
  struct Snapshot {
    ServerConfig server;
    ServerMetrics metrics;
    PingResult ping;
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

    virtual void onPingCycleStarted(size_t serverCount) { (void)serverCount; }
    virtual void onPingFinished(const ServerConfig& server, const PingResult& ping) {
      (void)server;
      (void)ping;
    }
    virtual void onPingCycleFinished(size_t polledCount, uint32_t durationMs) {
      (void)polledCount;
      (void)durationMs;
    }
  };

  MonitorService(ServerRegistry& registry, IMetricsProvider& provider, IPingProvider& pingProvider);

  void begin();
  void setListener(Listener* listener);

  // --- SSH metrics polling -------------------------------------------------
  // True when the configured interval elapsed or the web UI asked for an
  // immediate poll (the request flag is consumed by this call).
  bool pollDue();

  void pollAll();
  void requestPoll();

  bool isPolling() const;
  uint32_t lastCycleAtMs() const;

  // --- ICMP ping polling ---------------------------------------------------
  // Same contract as pollDue()/pollAll(), but driven by its own interval and
  // request flag so unreachable or slow SSH hosts never delay ping updates.
  bool pingDue();

  void pingAll();
  void requestPing();

  bool isPinging() const;
  uint32_t lastPingCycleAtMs() const;

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
  IPingProvider& pingProvider_;
  Listener* listener_ = nullptr;

  mutable SemaphoreHandle_t mutex_ = nullptr;
  std::map<String, ServerMetrics> metrics_;
  std::map<String, PingResult> pings_;

  std::atomic<bool> immediateRequest_{false};
  std::atomic<bool> polling_{false};
  uint32_t lastCycleAtMs_ = 0;

  std::atomic<bool> immediatePingRequest_{false};
  std::atomic<bool> pinging_{false};
  uint32_t lastPingCycleAtMs_ = 0;
};
