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
#include "infra/NvsWifiCredentialStore.h"
#include "infra/PingProvider.h"
#include "infra/SshMetricsProvider.h"
#include "infra/StatusLed.h"
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
constexpr uint32_t kPingTaskStack = 8 * 1024;
constexpr UBaseType_t kPingTaskPriority = 1;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kWifiRetryIntervalMs = 15000;

// Composition root: concrete implementations are chosen here and wired into
// the domain services.
NvsServerStore serverStore;
NvsWifiCredentialStore wifiCredentials;
ServerRegistry registry(serverStore);
SshMetricsProvider metricsProvider;
PingProvider pingProvider;
MonitorService monitor(registry, metricsProvider, pingProvider);
ConsoleView consoleView;
WebDashboard webDashboard(registry, monitor);
TftDashboard tftDashboard(monitor, registry);
StatusLed statusLed;

// Filled from NVS in setup(); the compile-time values are the fallback for a
// fresh device or one whose saved credentials were erased.
String wifiSsid = WIFI_SSID;
String wifiPassword = WIFI_PASSWORD;

TaskHandle_t pollTaskHandle = nullptr;
TaskHandle_t pingTaskHandle = nullptr;
uint32_t lastWifiAttemptMs = 0;
bool wifiWasConnected = false;
bool mdnsStarted = false;
bool webDashboardStarted = false;
bool webServerRestartPending = false;
bool networkSuspended = false;

void startMdns() {
  if (mdnsStarted) return;
  mdnsStarted = MDNS.begin(WIFI_HOSTNAME);
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
  }
  consoleView.printMdns(mdnsStarted);
}

void beginWifiStation() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiAttemptMs = millis();
}

// The Wi-Fi setup screen needs the radio quiet: both the boot-time association
// attempt and SSH polling otherwise compete with the display task while the
// user is trying to navigate. Everything is restored as soon as the screen
// closes.
void suspendNetworkForSetup() {
  if (networkSuspended) return;
  networkSuspended = true;

  Serial.println(F("Setup: network suspended"));
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  if (webDashboardStarted) {
    webDashboard.suspend();
    webServerRestartPending = true;
  }
  WiFi.disconnect(true);  // also powers the radio down
  WiFi.mode(WIFI_OFF);
  wifiWasConnected = false;
}

void resumeNetworkAfterSetup() {
  if (!networkSuspended) return;
  networkSuspended = false;

  String ssid;
  String password;
  if (tftDashboard.takeWifiCredentials(ssid, password)) {
    wifiSsid = ssid;
    wifiPassword = password;
    if (wifiCredentials.save(wifiSsid, wifiPassword)) {
      Serial.println(F("Setup: Wi-Fi credentials saved"));
    } else {
      Serial.println(F("Setup: could not save Wi-Fi credentials"));
    }
    Serial.printf("Setup: reconnecting Wi-Fi to \"%s\"\n", wifiSsid.c_str());
  } else {
    Serial.println(F("Setup: reconnecting Wi-Fi"));
  }

  beginWifiStation();
  wifiWasConnected = false;
}

void maintainSetupNetwork() {
  if (tftDashboard.isSetupOpen()) {
    suspendNetworkForSetup();
  } else {
    resumeNetworkAfterSetup();
  }
}

void connectWifiDuringBoot() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);

  consoleView.printWifiConnecting(wifiSsid.c_str());
  statusLed.setWifiConnecting(true);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  const uint32_t started = millis();
  uint32_t lastDotMs = started;
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < kWifiConnectTimeoutMs) {
    maintainSetupNetwork();

    const bool setupOpen = tftDashboard.isSetupOpen();
    statusLed.setWifiConnecting(!setupOpen);
    statusLed.update();

#if VPSPANEL_HAS_DISPLAY
    // The display task has not started yet; keep servicing the splash screen
    // so A can open the Wi-Fi setup while the station associates.
    tftDashboard.update();
#endif

    if (!setupOpen && (millis() - lastDotMs) >= 250) {
      lastDotMs = millis();
      Serial.print('.');
    }
    delay(10);
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
      if (webServerRestartPending) {
        // The listening socket was closed when the setup screen suspended the
        // network; reopen it now that an IP address is available again.
        webDashboard.resume();
        webServerRestartPending = false;
      }
      consoleView.printWifiConnected(WiFi.localIP().toString());
      startMdns();
      wifiWasConnected = true;
      monitor.requestPoll();
      monitor.requestPing();
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
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  }
}

// Runs SSH polls on a dedicated task so the web UI stays responsive while a
// remote host is slow or unreachable.
void pollTask(void*) {
  libssh_begin();

  for (;;) {
    // Do not start a new poll while the user is in the Wi-Fi setup screen: the
    // SSH connection would compete with the display for the radio/CPU.
    if (!tftDashboard.isSetupOpen()) {
      if (WiFi.status() == WL_CONNECTED && monitor.pollDue()) {
        monitor.pollAll();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ICMP pings have their own task and schedule, so a slow SSH cycle (or a host
// whose SSH port hangs) never delays reachability updates.
void pingTask(void*) {
  for (;;) {
    if (!tftDashboard.isSetupOpen()) {
      if (WiFi.status() == WL_CONNECTED && monitor.pingDue()) {
        monitor.pingAll();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

#if VPSPANEL_HAS_DISPLAY
// SPI drawing gets its own small task: a slow panel refresh must not delay
// HTTP requests or the SSH poller. The panel itself is initialised earlier in
// setup(), before Wi-Fi, so the splash screen is visible while the station
// associates; this task only replaces it with the dashboard.
//
// update() throttles its own redraws internally (1 s for the dashboard), so
// the loop only needs to be fast enough to notice a new poll cycle and a
// button press promptly.
void displayTask(void*) {
  for (;;) {
    tftDashboard.update();
    // 10 ms keeps even very short button taps from falling between two polls;
    // the renderer itself still throttles SPI traffic to once per second.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  consoleView.printBanner();

  if (wifiCredentials.load(wifiSsid, wifiPassword)) {
    Serial.println(F("Wi-Fi: using credentials saved in flash"));
  }

  statusLed.begin();

#if VPSPANEL_HAS_DISPLAY
  // Bring the panel up before touching the network. Initialising it after the
  // Wi-Fi association left the display blank for several seconds.
  tftDashboard.begin(wifiSsid, wifiPassword);
  Serial.println(F("Display: splash screen shown"));
#endif

  registry.begin();
  monitor.begin();
  monitor.setListener(&consoleView);

  consoleView.printConfiguration(registry.servers(), registry.settings(),
                                 registry.loadedFromStore());

  connectWifiDuringBoot();

  webDashboard.begin();
  webDashboardStarted = true;
  consoleView.printFreeHeap();

#if VPSPANEL_HAS_DISPLAY
  // The setup screen may still be open from the splash; its network suspension
  // closed no server yet, but the listener started while Wi-Fi was off, so ask
  // maintainWifi() to reopen it after the radio comes back.
  if (tftDashboard.isSetupOpen()) webServerRestartPending = true;

  // Let the splash screen hand over to the dashboard once the boot-time Wi-Fi
  // attempt is over. The display task takes over button polling from here.
  tftDashboard.finishBoot();

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

  if (xTaskCreate(pingTask, "vps-ping", kPingTaskStack, nullptr, kPingTaskPriority,
                  &pingTaskHandle) != pdPASS) {
    Serial.println(F("FATAL: could not start the ping task"));
  }
}

void loop() {
  maintainSetupNetwork();

  const bool setupOpen = tftDashboard.isSetupOpen();
  if (!setupOpen) {
    webDashboard.handle();
    maintainWifi();
  }

  statusLed.setWifiConnecting(!setupOpen && WiFi.status() != WL_CONNECTED);
  statusLed.setPolling(!setupOpen && (monitor.isPolling() || monitor.isPinging()));
  statusLed.update();

  delay(2);
}
