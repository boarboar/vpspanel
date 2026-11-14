#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"

// Presentation adapter: HTTP dashboard, server management page and JSON API.
// It only translates HTTP <-> domain calls; no polling logic lives here.
class WebDashboard {
 public:
  WebDashboard(ServerRegistry& registry, MonitorService& monitor, uint16_t port = 80);

  void begin();
  void handle();

 private:
  void handleDashboard();
  void handleServersPage();
  void handleSaveServer();
  void handleDeleteServer();
  void handleSaveSettings();
  void handleApiMetrics();
  void handleApiPoll();
  void handleNotFound();

  String renderPage(const String& title, const String& activeNav, const String& content) const;
  void redirectTo(const String& location);

  WebServer server_;
  ServerRegistry& registry_;
  MonitorService& monitor_;
};
