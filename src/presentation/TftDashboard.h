#pragma once

#include <Arduino.h>

#include <vector>

#include "config/display_config.h"
#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"
#include "presentation/WifiSetupDialog.h"

#if VPSPANEL_HAS_DISPLAY
#include <TFT_eSPI.h>

#include "infra/ButtonInput.h"
#endif

// Presentation adapter: renders the same monitor snapshots as the web
// dashboard on an ILI9341 TFT. Runs from its own FreeRTOS task, so SPI drawing
// never delays HTTP responses or SSH polling.
//
// Rendering is incremental: every widget remembers what it last drew and only
// fields whose value or colour changed are written to the panel. The dashboard
// is never cleared and repainted as a whole, which is what caused the visible
// flicker when the screen was refreshed every couple of seconds.
//
// Three buttons drive the screen. The physical B and C roles are swapped
// (VPSPANEL_SWAP_B_C): the B button enters Selection mode, A and C move the
// highlighted card, and B opens the details and returns to the dashboard.
// Selection mode is left automatically after 30 s without a button press.
// On the boot splash, A opens the Wi-Fi setup dialog instead.
class TftDashboard {
 public:
  TftDashboard(MonitorService& monitor, ServerRegistry& registry);

  // Initialise the panel and show the splash screen. Called from setup()
  // before the Wi-Fi connection is attempted, so the display is never blank
  // while the station associates. The credentials are the ones currently in
  // use, prefilled when the setup dialog opens.
  void begin(const String& wifiSsid, const String& wifiPassword);
  void update();  // cheap poll + incremental redraw; call frequently

  // Leaves the boot splash screen. Called once the Wi-Fi association attempt
  // has finished; from then on update() renders the dashboard.
  void finishBoot();

  // True while the Wi-Fi setup dialog is open. The rest of the firmware
  // suspends Wi-Fi/polling while this is set.
  bool isSetupOpen() const;

  // Copies credentials confirmed in the setup dialog, once. Returns false if
  // the user cancelled and nothing new was submitted.
  bool takeWifiCredentials(String& ssid, String& password);

 private:
#if VPSPANEL_HAS_DISPLAY
  static constexpr size_t kServersPerPage = 4;
  static constexpr size_t kSystemInfoRows = 7;

  enum class View : uint8_t {
    Splash,
    Setup,
    Dashboard,
    Detail,
    SystemMenu,
    SystemInfo,
  };

  // Which set of widgets occupies the lower half of a card. When it changes,
  // that area is cleared once before the new widgets are drawn.
  enum class CardLayout : uint8_t { None, Waiting, Error, Metrics };

  // Remembers what is currently visible so an unchanged value never touches
  // the SPI bus. The colour is part of the identity: the disk usage turns red
  // without its digits changing.
  //
  // Every call site always draws a given field with the same font, so a font
  // change never has to be handled here.
  struct TextField {
    String text;
    uint16_t color = 0;
    bool valid = false;
  };

  // One CPU/memory/disk gauge on a dashboard card.
  struct GaugeCache {
    int percent = -1;   // last rounded percentage, -1 when the metric has no data
    bool valid = false; // false until this gauge has been painted
  };

  struct CardCache {
    bool drawn = false;    // card background and border are on screen
    bool blank = false;    // slot deliberately cleared to the screen background
    bool selected = false; // border currently drawn in the selection colour
    CardLayout layout = CardLayout::None;
    TextField name;
    TextField chip;
    TextField ping;        // "OK 12ms" / "DOWN"
    GaugeCache gauges[3];  // CPU, MEM, DISK
    TextField note;        // "waiting for first poll..." / "polling is paused"
    TextField error;
  };

  // Fields of the per-server detail screen. The frame (background plus static
  // labels) is drawn once per visit; the values use the same incremental
  // TextField mechanism as the dashboard.
  struct DetailCache {
    bool frameDrawn = false;
    TextField name;
    TextField chip;
    TextField endpoint;
    TextField ping;   // reachability and round trip time
    TextField cpu;    // percentage
    TextField load;   // "LA 1.24"
    TextField cores;  // "8 cores"
    TextField mem;
    TextField disk;
    TextField info;      // SSH state: uptime or failure summary
    TextField pingInfo;  // PING state: RTT or failure summary
    int cpuBarFilled = -1;
    uint16_t cpuBarColor = 0;
    int barFilled = -1;
    uint16_t barColor = 0;
    int diskBarFilled = -1;
    uint16_t diskBarColor = 0;
  };

  void drawSplash(const String& wifiSsid);
  // Snapshots of the servers that should appear on the panel. Paused servers
  // are skipped, so they never take a card, a page slot or a selection stop.
  std::vector<MonitorService::Snapshot> visibleSnapshots() const;
  void renderDashboard(const std::vector<MonitorService::Snapshot>& snapshots, size_t pages);
  void ensureChrome();
  void updateHeader();
  void updateFooter(size_t pages);
  void renderCard(const MonitorService::Snapshot& snapshot, size_t slot, bool selected);
  void drawCardChrome(int x, int y, bool selected);
  void drawCardBorder(int x, int y, bool selected);
  void drawEmptyCard();

  void drawField(TextField& field, const String& value, int x, int y, uint8_t font,
                 uint16_t color, int clearToX);
  void drawRightField(TextField& field, const String& value, int rightX, int y,
                      int clearX, int clearW, uint8_t font, uint16_t color);
  String footerText() const;

  void handleButtons(uint32_t now);
  void openSetup();
  void closeSetup(uint32_t now, bool ok);
  void enterSelection(uint32_t now);
  void moveSelection(int delta, uint32_t now);
  void exitSelection(uint32_t now);

  void openDetail(const String& serverId);
  void closeDetail(uint32_t now);
  void resetDetail();
  void invalidateDashboard();
  void drawDetailFrame();
  void renderDetail(const MonitorService::Snapshot& snapshot);

  void openSystemMenu();
  void closeSystemMenu(uint32_t now);
  void moveSystemMenu(int delta);
  void activateSystemMenu();
  void drawSystemMenu();

  void openSystemInfo();
  void closeSystemInfo();
  void drawSystemInfoFrame();
  void renderSystemInfo();

  TFT_eSPI tft_;
  ButtonInput buttons_;
  View view_ = View::Splash;
  String detailId_;
  String wifiSsid_;
  String wifiPassword_;

  bool bootFinished_ = false;
  uint32_t splashHoldUntilMs_ = 0;

  bool selecting_ = false;
  size_t selectedIndex_ = 0;            // index into MonitorService::snapshots()
  uint32_t lastSelectionActivityMs_ = 0;
  bool forceRender_ = false;

  CardCache cards_[kServersPerPage];
  TextField headerRight_;
  TextField footerLeft_;
  TextField footerPage_;
  TextField emptyUrl_;
  bool chromeDrawn_ = false;
  bool emptyDrawn_ = false;
  bool emptyAllPaused_ = false;
  uint32_t lastPageMs_ = 0;

  DetailCache detail_;

  // System menu state. The menu is redrawn only when its selection changes.
  uint8_t menuSelection_ = 0;
  uint8_t menuDrawnSelection_ = 0;
  bool menuDrawn_ = false;

  // System info screen: the frame (labels and separators) is drawn once per
  // visit, the values are refreshed once per second.
  bool infoFrameDrawn_ = false;
  uint32_t lastInfoRefreshMs_ = 0;
  TextField infoValues_[kSystemInfoRows];
#endif

  MonitorService& monitor_;
  ServerRegistry& registry_;

  WifiSetupDialog setupDialog_;

  bool initialized_ = false;
  bool hasRendered_ = false;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastCycleSeenMs_ = 0;
  size_t page_ = 0;
};
