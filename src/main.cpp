#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#if __has_include("config/wifi_credentials.h")
#include "config/wifi_credentials.h"
#else
#error "Missing src/config/wifi_credentials.h - copy src/config/wifi_credentials.example.h and fill it in."
#endif

#include "libssh_esp32.h"

#include "config/display_config.h"
#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"
#include "infra/NvsServerStore.h"
#include "infra/SshMetricsProvider.h"
#include "infra/StatusLed.h"
#include "infra/TouchCalibrationStore.h"
#include "presentation/ConsoleView.h"
#include "presentation/TftDashboard.h"
#include "presentation/WebDashboard.h"

#ifndef WIFI_HOSTNAME
#define WIFI_HOSTNAME "vpspanel"
#endif

// The loop task runs the web server; SSH gets its own task (below).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr uint32_t kPollTaskStack = 40 * 1024;
constexpr UBaseType_t kPollTaskPriority = 1;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kWifiRetryIntervalMs = 15000;

// Composition root: concrete implementations are chosen here and wired into
// the domain services.
NvsServerStore serverStore;
ServerRegistry registry(serverStore);
SshMetricsProvider metricsProvider;
MonitorService monitor(registry, metricsProvider);
ConsoleView consoleView;
WebDashboard webDashboard(registry, monitor);
TouchCalibrationStore touchCalibration;
TftDashboard tftDashboard(monitor, registry, touchCalibration);
StatusLed statusLed;

TaskHandle_t pollTaskHandle = nullptr;
uint32_t lastWifiAttemptMs = 0;
bool wifiWasConnected = false;
bool mdnsStarted = false;

void startMdns() {
  if (mdnsStarted) return;
  mdnsStarted = MDNS.begin(WIFI_HOSTNAME);
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
  }
  consoleView.printMdns(mdnsStarted);
}

void connectWifiDuringBoot() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);

  consoleView.printWifiConnecting(WIFI_SSID);
  statusLed.setWifiConnecting(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t started = millis();
  uint32_t lastDotMs = started;
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < kWifiConnectTimeoutMs) {
    statusLed.update();
    if ((millis() - lastDotMs) >= 250) {
      lastDotMs = millis();
      Serial.print('.');
    }
    delay(20);
  }

  if (WiFi.status() == WL_CONNECTED) {
    statusLed.setWifiConnecting(false);
    statusLed.update();
    consoleView.printWifiConnected(WiFi.localIP().toString());
    wifiWasConnected = true;
    startMdns();
  } else {
    Serial.println(F("\nWi-Fi: not connected yet, retrying in the background"));
  }
}

void maintainWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    if (!wifiWasConnected) {
      consoleView.printWifiConnected(WiFi.localIP().toString());
      startMdns();
      wifiWasConnected = true;
      monitor.requestPoll();
    }
    return;
  }

  if (wifiWasConnected) {
    consoleView.printWifiLost();
    wifiWasConnected = false;
  }

  if ((millis() - lastWifiAttemptMs) >= kWifiRetryIntervalMs) {
    lastWifiAttemptMs = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// Runs SSH polls on a dedicated task so the web UI stays responsive while a
// remote host is slow or unreachable.
void pollTask(void*) {
  libssh_begin();

  for (;;) {
    if (WiFi.status() == WL_CONNECTED && monitor.pollDue()) {
      monitor.pollAll();
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

#if VPSPANEL_HAS_DISPLAY
// SPI drawing gets its own small task: a slow panel refresh must not delay
// HTTP requests or the SSH poller. The panel itself is initialised earlier in
// setup(), before Wi-Fi, so the splash screen is visible while the station
// associates; this task only replaces it with the dashboard.
//
// The loop runs fast because the same task also samples the touch controller.
// A tap only lasts ~100 ms, so a slow loop (it used to be 250 ms) misses most
// presses entirely. update() is cheap when nothing changed: the redraws
// themselves are throttled internally.
void displayTask(void*) {
  for (;;) {
    tftDashboard.update();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  consoleView.printBanner();

  statusLed.begin();

#if VPSPANEL_HAS_DISPLAY
  // Bring the panel up before touching the network. Initialising it after the
  // Wi-Fi association left the display blank for several seconds.
  tftDashboard.begin(WIFI_SSID);
  Serial.println(F("Display: splash screen shown"));
#endif

  registry.begin();
  monitor.begin();
  monitor.setListener(&consoleView);

  consoleView.printConfiguration(registry.servers(), registry.settings(),
                                 registry.loadedFromStore());

  connectWifiDuringBoot();

  webDashboard.begin();
  consoleView.printFreeHeap();

#if VPSPANEL_HAS_DISPLAY
  if (xTaskCreate(displayTask, "display", 10 * 1024, nullptr, 1, nullptr) != pdPASS) {
    Serial.println(F("display: could not start task"));
  } else {
    Serial.println(F("Display: ILI9341 dashboard started"));
  }
#endif

  if (xTaskCreate(pollTask, "vps-poll", kPollTaskStack, nullptr, kPollTaskPriority,
                  &pollTaskHandle) != pdPASS) {
    Serial.println(F("FATAL: could not start the polling task"));
  }
}

void loop() {
  webDashboard.handle();
  maintainWifi();

  statusLed.setWifiConnecting(WiFi.status() != WL_CONNECTED);
  statusLed.setPolling(monitor.isPolling());
  statusLed.update();

  delay(2);
}
