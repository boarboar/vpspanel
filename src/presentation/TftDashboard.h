#pragma once

#include <Arduino.h>

#include <atomic>
#include <vector>

#include "config/display_config.h"
#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"

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
// Three buttons drive the screen: C enters Selection mode, A/B move the
// highlighted card, C opens its details and C returns to the dashboard.
// Selection mode is left automatically after 30 s without a button press.
// On the boot splash, A opens a placeholder Wi-Fi setup screen instead.
class TftDashboard {
 public:
  TftDashboard(MonitorService& monitor, ServerRegistry& registry);

  // Initialise the panel and show the splash screen. Called from setup()
  // before the Wi-Fi connection is attempted, so the display is never blank
  // while the station associates.
  void begin(const String& wifiSsid);
  void update();  // cheap poll + incremental redraw; call frequently

  // Leaves the boot splash screen. Called once the Wi-Fi association attempt
  // has finished; from then on update() renders the dashboard.
  void finishBoot();

  // True while the placeholder Wi-Fi setup screen is open. The rest of the
  // firmware suspends Wi-Fi/polling while this is set.
  bool isSetupOpen() const { return setupOpen_.load(); }

 private:
#if VPSPANEL_HAS_DISPLAY
  static constexpr size_t kServersPerPage = 2;
  static constexpr uint8_t kSetupChoices = 2;  // OK and Cancel

  enum class View : uint8_t { Splash, Setup, Dashboard, Detail };

  // Which set of widgets occupies the lower half of a card. When it changes,
  // that area is cleared once before the new widgets are drawn.
  enum class CardLayout : uint8_t { None, Waiting, Error, Metrics };

  // Remembers what is currently visible so an unchanged value never touches
  // the SPI bus. The colour is part of the identity: the temperature turns red
  // without its digits changing.
  //
  // Every call site always draws a given field with the same font, so a font
  // change never has to be handled here.
  struct TextField {
    String text;
    uint16_t color = 0;
    bool valid = false;
  };

  struct CardCache {
    bool drawn = false;    // card background and border are on screen
    bool blank = false;    // slot deliberately cleared to the screen background
    bool selected = false; // border currently drawn in the selection colour
    CardLayout layout = CardLayout::None;
    bool labelsDrawn = false;
    TextField name;
    TextField chip;
    uint16_t chipColor = 0;
    TextField endpoint;
    TextField load;
    TextField mem;
    TextField temp;
    TextField note;   // "waiting for first poll..." / "polling is paused"
    TextField error;
    int barFilled = -1;  // pixels currently drawn in the memory bar
    uint16_t barColor = 0;
  };

  // Fields of the per-server detail screen. The frame (background plus static
  // labels) is drawn once per visit; the values use the same incremental
  // TextField mechanism as the dashboard.
  struct DetailCache {
    bool frameDrawn = false;
    TextField name;
    TextField chip;
    TextField endpoint;
    TextField load;
    TextField mem;
    TextField temp;
    TextField age;
    TextField took;
    TextField note;
    uint8_t noteKind = 0;  // 0 = nothing, 1 = waiting, 2 = error
    int barFilled = -1;
    uint16_t barColor = 0;
  };

  void drawSplash(const String& wifiSsid);
  void renderDashboard(const std::vector<MonitorService::Snapshot>& snapshots, size_t pages);
  void ensureChrome();
  void updateHeader();
  void updateFooter(size_t pages);
  void renderCard(const MonitorService::Snapshot& snapshot, size_t slot, bool selected);
  void drawCardChrome(int x, int y, bool selected);
  void drawCardBorder(int x, int y, bool selected);
  void drawEmptyCard();

  void drawSetupScreen();
  void drawSetupButtons();
  void drawSetupButton(int x, int y, int w, int h, const char* label, bool selected);
  void drawField(TextField& field, const String& value, int x, int y, uint8_t font,
                 uint16_t color, int clearToX);
  void drawRightField(TextField& field, const String& value, int rightX, int y,
                      int clearX, int clearW, uint8_t font, uint16_t color);
  String footerText() const;

  void handleButtons(uint32_t now);
  void openSetup();
  void closeSetup(uint32_t now);
  void moveSetupSelection(int delta);
  void enterSelection(uint32_t now);
  void moveSelection(int delta, uint32_t now);
  void exitSelection(uint32_t now);

  void openDetail(const String& serverId);
  void closeDetail(uint32_t now);
  void resetDetail();
  void invalidateDashboard();
  void drawDetailFrame();
  void renderDetail(const MonitorService::Snapshot& snapshot);

  TFT_eSPI tft_;
  ButtonInput buttons_;
  View view_ = View::Splash;
  String detailId_;
  String wifiSsid_;

  bool bootFinished_ = false;
  bool setupFrameDrawn_ = false;
  uint8_t setupSelection_ = 0;
  uint8_t setupDrawnSelection_ = 0;
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
  uint32_t lastPageMs_ = 0;

  DetailCache detail_;
#endif

  MonitorService& monitor_;
  ServerRegistry& registry_;

  std::atomic<bool> setupOpen_{false};

  bool initialized_ = false;
  bool hasRendered_ = false;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastCycleSeenMs_ = 0;
  size_t page_ = 0;
};
