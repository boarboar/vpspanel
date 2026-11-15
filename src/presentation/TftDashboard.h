#pragma once

#include <Arduino.h>

#include <vector>

#include "config/display_config.h"
#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"

#if VPSPANEL_HAS_DISPLAY
#include <TFT_eSPI.h>
#endif

class TouchCalibrationStore;

// Presentation adapter: renders the same monitor snapshots as the web
// dashboard on an ILI9341 TFT. Runs from its own FreeRTOS task, so SPI drawing
// never delays HTTP responses or SSH polling.
//
// Rendering is incremental: every widget remembers what it last drew and only
// fields whose value or colour changed are written to the panel. The dashboard
// is never cleared and repainted as a whole, which is what caused the visible
// flicker when the screen was refreshed every couple of seconds.
//
// An XPT2046 resistive touch controller (sharing the display SPI bus) is used
// to open a per-server detail screen: tapping a card shows it, tapping the
// detail screen returns to the dashboard.
class TftDashboard {
 public:
  TftDashboard(MonitorService& monitor, ServerRegistry& registry,
               TouchCalibrationStore& touchCalibration);

  // Initialise the panel, show the splash screen and (on first boot) offer
  // touch calibration. Called from setup() before the Wi-Fi connection is
  // attempted, so the display is never blank while the station associates.
  void begin(const String& wifiSsid);
  void update();  // cheap poll + incremental redraw; call frequently

 private:
#if VPSPANEL_HAS_DISPLAY
  static constexpr size_t kServersPerPage = 2;
  static constexpr size_t kTouchCalValues = 5;

  enum class View : uint8_t { Dashboard, Detail };

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
    bool drawn = false;   // card background and border are on screen
    bool blank = false;   // slot deliberately cleared to the screen background
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
  void renderCard(const MonitorService::Snapshot& snapshot, size_t slot);
  void drawCardChrome(int x, int y);
  void drawEmptyCard();
  void drawField(TextField& field, const String& value, int x, int y, uint8_t font,
                 uint16_t color, int clearToX);
  void drawRightField(TextField& field, const String& value, int rightX, int y,
                      int clearX, int clearW, uint8_t font, uint16_t color);
  String footerText() const;

  void calibrateTouch();
  bool touchPressedSignal(uint16_t z) const;
  bool readTouchDirect(uint16_t& rawX, uint16_t& rawY);
  bool readTouch(uint16_t& x, uint16_t& y);
  void handleTouch();

  void openDetail(const String& serverId);
  void closeDetail();
  void resetDetail();
  void invalidateDashboard();
  void drawDetailFrame();
  void renderDetail(const MonitorService::Snapshot& snapshot);

  TFT_eSPI tft_;
  TouchCalibrationStore& touchCalibration_;
  uint16_t touchCal_[kTouchCalValues] = {
      VPSPANEL_TOUCH_CAL_X0, VPSPANEL_TOUCH_CAL_X1, VPSPANEL_TOUCH_CAL_Y0,
      VPSPANEL_TOUCH_CAL_Y1, VPSPANEL_TOUCH_CAL_FLAGS};
  View view_ = View::Dashboard;
  String detailId_;
  bool touchDown_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastTouchDebugMs_ = 0;
  bool touchDebugPressed_ = false;

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

  bool initialized_ = false;
  bool hasRendered_ = false;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastCycleSeenMs_ = 0;
  size_t page_ = 0;
};
