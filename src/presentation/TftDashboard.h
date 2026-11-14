#pragma once

#include <Arduino.h>

#include <vector>

#include "config/display_config.h"
#include "domain/MonitorService.h"
#include "domain/ServerRegistry.h"

#if VPSPANEL_HAS_DISPLAY
#include <TFT_eSPI.h>
#endif

// Presentation adapter: renders the same monitor snapshots as the web
// dashboard on an ILI9341 TFT. Runs from its own FreeRTOS task, so SPI drawing
// never delays HTTP responses or SSH polling.
//
// Rendering is incremental: every widget remembers what it last drew and only
// fields whose value or colour changed are written to the panel. The dashboard
// is never cleared and repainted as a whole, which is what caused the visible
// flicker when the screen was refreshed every couple of seconds.
class TftDashboard {
 public:
  TftDashboard(MonitorService& monitor, ServerRegistry& registry);

  void begin();   // initialise the panel and draw the splash screen
  void update();  // cheap poll + incremental redraw; call frequently

 private:
#if VPSPANEL_HAS_DISPLAY
  static constexpr size_t kServersPerPage = 2;

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

  void drawSplash();
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

  TFT_eSPI tft_;
  CardCache cards_[kServersPerPage];
  TextField headerRight_;
  TextField footerLeft_;
  TextField footerPage_;
  TextField emptyUrl_;
  bool chromeDrawn_ = false;
  bool emptyDrawn_ = false;
  uint32_t lastPageMs_ = 0;
#endif

  MonitorService& monitor_;
  ServerRegistry& registry_;

  bool initialized_ = false;
  bool hasRendered_ = false;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastCycleSeenMs_ = 0;
  size_t page_ = 0;
};
