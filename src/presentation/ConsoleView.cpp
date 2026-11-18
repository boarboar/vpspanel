#include "presentation/ConsoleView.h"

#include <cmath>

#include "domain/ServerRegistry.h"

namespace {

String formatValue(float value, uint8_t digits = 1) {
  if (isnan(value)) return String("n/a");
  return String(value, static_cast<unsigned int>(digits));
}

String formatDisk(const ServerMetrics& metrics) {
  if (isnan(metrics.diskUsedMb) || isnan(metrics.diskTotalMb) || metrics.diskTotalMb <= 0) {
    return String("n/a");
  }

  const float percent = 100.0f * metrics.diskUsedMb / metrics.diskTotalMb;
  return formatValue(metrics.diskUsedMb, 0) + "/" + formatValue(metrics.diskTotalMb, 0) +
         " MB (" + formatValue(percent, 0) + "%)";
}

}  // namespace

void ConsoleView::printBanner() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  VPS Monitor - ESP32 SSH dashboard"));
  Serial.println(F("========================================"));
}

void ConsoleView::printWifiConnecting(const char* ssid) {
  Serial.printf("Wi-Fi: connecting to \"%s\"", ssid);
}

void ConsoleView::printWifiConnected(const String& ip) {
  Serial.printf("\nWi-Fi: connected, IP %s\n", ip.c_str());
  Serial.printf("Web UI: http://%s/\n", ip.c_str());
}

void ConsoleView::printWifiLost() {
  Serial.println(F("Wi-Fi: connection lost, retrying in background"));
}

void ConsoleView::printConfiguration(const std::vector<ServerConfig>& servers,
                                     const AppSettings& settings, bool loadedFromStore) {
  Serial.printf("Storage: %s\n",
                loadedFromStore ? "configuration loaded from flash (NVS)"
                                : "no saved configuration, using defaults + empty server list");
  Serial.printf("Settings: poll every %u s, SSH timeout %u s\n",
                settings.pollIntervalSec, settings.sshTimeoutSec);
  Serial.printf("Servers: %u configured (max %u)\n",
                static_cast<unsigned>(servers.size()),
                static_cast<unsigned>(ServerRegistry::kMaxServers));

  size_t index = 1;
  for (const auto& server : servers) {
    Serial.printf("  %u. %s  %s  user=%s  %s\n", static_cast<unsigned>(index++),
                  server.displayName().c_str(), server.endpoint().c_str(),
                  server.user.c_str(), server.enabled ? "enabled" : "paused");
  }
}

void ConsoleView::printMdns(bool started) {
  if (started) {
    Serial.println(F("mDNS: http://vpspanel.local/"));
  } else {
    Serial.println(F("mDNS: failed to start"));
  }
}

void ConsoleView::printFreeHeap() {
  Serial.printf("Free heap: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
}

void ConsoleView::onCycleStarted(size_t serverCount) {
  Serial.printf("[poll] checking %u server(s)\n", static_cast<unsigned>(serverCount));
}

void ConsoleView::onPollFinished(const ServerConfig& server, const ServerMetrics& metrics) {
  if (!metrics.ok) {
    Serial.printf("[fail] %s (%s): %s\n", server.displayName().c_str(),
                  server.endpoint().c_str(), metrics.error.c_str());
    return;
  }

  Serial.printf("[ ok ] %s (%s) cpu=%s%% load=%s mem=%s/%s MB disk=%s (%u ms)\n",
                server.displayName().c_str(), server.endpoint().c_str(),
                formatValue(metrics.cpuPercent, 1).c_str(),
                formatValue(metrics.load1, 2).c_str(),
                formatValue(metrics.memUsedMb, 0).c_str(),
                formatValue(metrics.memTotalMb, 0).c_str(),
                formatDisk(metrics).c_str(),
                static_cast<unsigned>(metrics.durationMs));
}

void ConsoleView::onCycleFinished(size_t polledCount, uint32_t durationMs) {
  Serial.printf("[poll] done: %u server(s) in %u ms\n\n",
                static_cast<unsigned>(polledCount), static_cast<unsigned>(durationMs));
}
