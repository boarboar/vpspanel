#pragma once

#include <Arduino.h>

#include "domain/AppSettings.h"
#include "domain/MonitorService.h"
#include "domain/ServerConfig.h"

// Presentation adapter: renders monitor events and boot info to Serial.
class ConsoleView : public MonitorService::Listener {
 public:
  void printBanner();
  void printWifiConnecting(const char* ssid);
  void printWifiConnected(const String& ip);
  void printWifiLost();
  void printConfiguration(const std::vector<ServerConfig>& servers, const AppSettings& settings,
                          bool loadedFromStore);
  void printMdns(bool started);
  void printFreeHeap();

  void onCycleStarted(size_t serverCount) override;
  void onPollFinished(const ServerConfig& server, const ServerMetrics& metrics) override;
  void onCycleFinished(size_t polledCount, uint32_t durationMs) override;
};
