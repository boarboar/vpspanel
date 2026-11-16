#include "presentation/TftDashboard.h"

#include <cmath>

#if VPSPANEL_HAS_DISPLAY

#include <WiFi.h>

#include "config/input_config.h"

// TFT_eSPI gets its pins from platformio.ini build_flags. These asserts fail
// the build if those flags drift from src/config/display_config.h, so the
// library and the dashboard can never silently use different pins.
static_assert(TFT_MISO == VPSPANEL_TFT_MISO, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_MOSI == VPSPANEL_TFT_MOSI, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_SCLK == VPSPANEL_TFT_SCLK, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_CS == VPSPANEL_TFT_CS, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_DC == VPSPANEL_TFT_DC, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_RST == VPSPANEL_TFT_RST, "TFT_eSPI pins do not match display_config.h");
static_assert(TFT_BL == VPSPANEL_TFT_BL, "TFT_eSPI backlight does not match display_config.h");
static_assert(TFT_WIDTH == 240 && TFT_HEIGHT == 320, "Expected a 240x320 ILI9341 panel");

namespace {

// Same palette as the web dashboard (RGB565).
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg = rgb565(15, 20, 32);
constexpr uint16_t kPanel = rgb565(23, 30, 46);
constexpr uint16_t kPanel2 = rgb565(30, 39, 57);
constexpr uint16_t kLine = rgb565(42, 53, 80);
constexpr uint16_t kText = rgb565(231, 236, 245);
constexpr uint16_t kMuted = rgb565(147, 161, 189);
constexpr uint16_t kAccent = rgb565(77, 163, 255);
constexpr uint16_t kOk = rgb565(61, 220, 151);
constexpr uint16_t kWarn = rgb565(255, 180, 84);
constexpr uint16_t kErr = rgb565(255, 107, 107);
constexpr uint16_t kSelected = rgb565(255, 214, 102);  // Selection-mode border

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kHeaderH = 30;
constexpr int kFooterY = 218;

constexpr int kCardX = 5;
constexpr int kCardW = 310;
constexpr int kCardH = 88;
constexpr int kCardGap = 6;
constexpr int kCardY0 = 34;

// How often the panel contents are re-evaluated. Actual SPI traffic only
// happens for fields whose value or colour changed in the meantime.
constexpr uint32_t kRefreshMs = 1000;
constexpr uint32_t kPageMs = 7000;

// The detail screen carries a poll-age field that only changes once a second,
// so redrawing it faster than this is pointless.
constexpr uint32_t kDetailRefreshMs = 250;

// After closing the setup screen, keep the resumed splash visible for a moment
// even if the boot-time Wi-Fi attempt has already finished.
constexpr uint32_t kSplashResumeHoldMs = 2000;

constexpr int kBarX = 95;
constexpr int kBarY = 75;
constexpr int kBarW = 130;
constexpr int kBarH = 6;

// Detail screen layout.
constexpr int kDetailHeaderH = 28;
constexpr int kDetailDivider1Y = 80;
constexpr int kDetailDivider2Y = 130;
constexpr int kDetailBarX = 10;
constexpr int kDetailBarY = 166;
constexpr int kDetailBarW = 300;
constexpr int kDetailBarH = 6;
constexpr int kDetailNoteY = 196;

// Placeholder Wi-Fi setup screen.
constexpr int kSetupHeaderH = 28;
constexpr int kSetupButtonY = 146;
constexpr int kSetupButtonW = 120;
constexpr int kSetupButtonH = 48;
constexpr int kSetupOkX = 25;
constexpr int kSetupCancelX = kScreenW - kSetupOkX - kSetupButtonW;

int cardY(size_t slot) {
  return kCardY0 + static_cast<int>(slot) * (kCardH + kCardGap);
}

String fitText(const String& text, size_t maxChars) {
  if (text.length() <= maxChars) return text;
  if (maxChars <= 3) return text.substring(0, maxChars);
  return text.substring(0, maxChars - 3) + "...";
}

// Truncate `text` so that it fits into `maxWidth` pixels of `font`. Unlike
// fitText() this respects the proportional fonts, so long values never run
// into the neighbouring field.
String ellipsize(TFT_eSPI& tft, const String& text, uint8_t font, int maxWidth) {
  if (maxWidth <= 0) return String();
  if (tft.textWidth(text, font) <= maxWidth) return text;

  String truncated = text;
  while (truncated.length() > 0 && tft.textWidth(truncated + "...", font) > maxWidth) {
    truncated.remove(truncated.length() - 1);
  }
  return truncated.length() > 0 ? truncated + "..." : String();
}

void drawBar(TFT_eSPI& tft, int x, int y, int width, int height, float percent,
             uint16_t color) {
  tft.fillRect(x, y, width, height, kPanel2);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const int filled = static_cast<int>((width * percent) / 100.0f + 0.5f);
  if (filled > 0) tft.fillRect(x, y, filled, height, color);
}

// Font 1 word wrap, used for SSH error messages.
void drawWrapped(TFT_eSPI& tft, const String& text, int x, int y, int maxX, int maxLines,
                 uint16_t color, uint16_t background) {
  const int maxChars = (maxX - x) / 6;
  if (maxChars < 8 || maxLines < 1) return;

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, background);

  String line;
  int lines = 0;
  int start = 0;

  while (start <= static_cast<int>(text.length()) && lines < maxLines) {
    const int space = text.indexOf(' ', start);
    const String word = space < 0 ? text.substring(start) : text.substring(start, space);
    const String candidate = line.length() > 0 ? line + " " + word : word;

    if (candidate.length() > static_cast<size_t>(maxChars) && line.length() > 0) {
      tft.drawString(fitText(line, maxChars), x, y + lines * 10, 1);
      ++lines;
      line = word;
    } else {
      line = candidate;
    }

    if (space < 0) break;
    start = space + 1;
  }

  if (lines < maxLines && line.length() > 0) {
    tft.drawString(fitText(line, maxChars), x, y + lines * 10, 1);
  }
}

}  // namespace

TftDashboard::TftDashboard(MonitorService& monitor, ServerRegistry& registry)
    : tft_(), monitor_(monitor), registry_(registry) {}

void TftDashboard::begin(const String& wifiSsid) {
  tft_.init();
  tft_.setRotation(VPSPANEL_TFT_ROTATION);

  buttons_.begin();

  wifiSsid_ = wifiSsid;
  view_ = View::Splash;
  setupOpen_.store(false);

  drawSplash(wifiSsid_);

  initialized_ = true;
  lastPageMs_ = millis();
}

void TftDashboard::finishBoot() {
  bootFinished_ = true;
}

void TftDashboard::update() {
  if (!initialized_) return;

  const uint32_t now = millis();

  // Buttons must be serviced even when the incremental renderer decides that
  // there is nothing to redraw.
  handleButtons(now);

  if (view_ == View::Dashboard && selecting_ &&
      (now - lastSelectionActivityMs_) >= VPSPANEL_SELECTION_TIMEOUT_MS) {
    exitSelection(now);
  }

  // The setup screen has no live data; redraw it only when its selection
  // changes.
  if (view_ == View::Setup) {
    if (!setupFrameDrawn_) {
      drawSetupScreen();
      setupFrameDrawn_ = true;
      setupDrawnSelection_ = setupSelection_;
    } else if (setupSelection_ != setupDrawnSelection_) {
      drawSetupButtons();
      setupDrawnSelection_ = setupSelection_;
    }
    return;
  }

  if (view_ == View::Splash) {
    // The splash stays visible while the station associates, which is also the
    // only time A opens the Wi-Fi setup screen.
    if (!bootFinished_) return;
    if (static_cast<int32_t>(now - splashHoldUntilMs_) < 0) return;
    view_ = View::Dashboard;
  }

  if (view_ == View::Detail) {
    // Only the poll-age field changes, and only once a second, so there is no
    // point redrawing the detail screen on every update() call.
    if (detail_.frameDrawn && (now - lastRenderMs_) < kDetailRefreshMs) return;

    const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
    const MonitorService::Snapshot* detail = nullptr;
    for (const auto& snapshot : snapshots) {
      if (snapshot.server.id == detailId_) {
        detail = &snapshot;
        break;
      }
    }

    if (detail == nullptr) {
      // The server disappeared from the configuration while its page was up.
      closeDetail(now);
    } else {
      if (!detail_.frameDrawn) {
        drawDetailFrame();
        detail_.frameDrawn = true;
      }
      renderDetail(*detail);
      lastRenderMs_ = now;
      return;
    }
  }

  const uint32_t cycle = monitor_.lastCycleAtMs();
  const bool newCycle = cycle != lastCycleSeenMs_;
  const bool forced = forceRender_;
  forceRender_ = false;

  if (hasRendered_ && !newCycle && !forced && (now - lastRenderMs_) < kRefreshMs) return;
  lastCycleSeenMs_ = cycle;

  const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
  size_t pages = snapshots.empty()
                     ? 1
                     : (snapshots.size() + kServersPerPage - 1) / kServersPerPage;

  if (snapshots.empty()) {
    if (selecting_) exitSelection(now);
  } else if (selecting_) {
    // The selection may point past the end after a server was removed. The
    // selected card also decides which page is visible while selecting, so
    // A/B can move across page boundaries.
    if (selectedIndex_ >= snapshots.size()) selectedIndex_ = snapshots.size() - 1;
    page_ = selectedIndex_ / kServersPerPage;
  }

  // Automatic paging is suspended while a card is selected; the user drives
  // the page with A/B instead.
  if (!selecting_ && pages > 1 && hasRendered_ && (now - lastPageMs_) >= kPageMs) {
    page_ = (page_ + 1) % pages;
    lastPageMs_ = now;
  }
  if (page_ >= pages) page_ = 0;

  if (!hasRendered_) {
    // Only the splash screen has to be erased; after that the dashboard is
    // updated field by field.
    tft_.fillScreen(kBg);
  }

  renderDashboard(snapshots, pages);

  lastRenderMs_ = now;
  if (!hasRendered_) {
    hasRendered_ = true;
    lastPageMs_ = now;
  }
}

void TftDashboard::drawSplash(const String& wifiSsid) {
  // The splash is also redrawn when the setup screen closes, so it must clear
  // the whole panel instead of drawing over the setup widgets.
  tft_.fillScreen(kBg);

  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("VPS MONITOR", kScreenW / 2, kScreenH / 2 - 40, 4);

  // Tell the user which network the device is about to join; the panel stays
  // on this screen until the first dashboard render.
  const String label = "Wi-Fi: " + wifiSsid;
  tft_.setTextColor(kText, kBg);
  tft_.drawString(ellipsize(tft_, label, 2, kScreenW - 20), kScreenW / 2,
                  kScreenH / 2 + 6, 2);

  const bool online = WiFi.status() == WL_CONNECTED;
  tft_.setTextColor(online ? kOk : kMuted, kBg);
  tft_.drawString(online ? "connected" : "connecting...", kScreenW / 2,
                  kScreenH / 2 + 32, 1);

  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("press A to setup WiFi", kScreenW / 2, kScreenH - 18, 1);
}

void TftDashboard::drawSetupScreen() {
  tft_.fillScreen(kPanel);
  tft_.fillRect(0, 0, kScreenW, kSetupHeaderH, kBg);

  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("WIFI SETUP", 8, 5, 2);

  tft_.setTextDatum(TR_DATUM);
  tft_.setTextColor(kMuted, kBg);
  tft_.drawString("A/B: select  C: press", kScreenW - 8, 8, 1);

  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(kText, kPanel);
  tft_.drawString("Wi-Fi setup placeholder", kScreenW / 2, 70, 2);

  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString("Network configuration will be added here.", kScreenW / 2, 98, 1);

  drawSetupButtons();
}

void TftDashboard::drawSetupButtons() {
  drawSetupButton(kSetupOkX, kSetupButtonY, kSetupButtonW, kSetupButtonH, "OK",
                  setupSelection_ == 0);
  drawSetupButton(kSetupCancelX, kSetupButtonY, kSetupButtonW, kSetupButtonH, "Cancel",
                  setupSelection_ == 1);
}

void TftDashboard::drawSetupButton(int x, int y, int w, int h, const char* label,
                                   bool selected) {
  const uint16_t color = selected ? kSelected : kText;
  tft_.fillRoundRect(x, y, w, h, 8, kPanel2);
  tft_.drawRoundRect(x, y, w, h, 8, selected ? kSelected : kLine);

  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(color, kPanel2);
  tft_.drawString(label, x + w / 2, y + h / 2, 2);
}

void TftDashboard::renderDashboard(const std::vector<MonitorService::Snapshot>& snapshots,
                                   size_t pages) {
  tft_.startWrite();

  ensureChrome();
  updateHeader();
  updateFooter(pages);

  if (snapshots.empty()) {
    // The cards are replaced by one tall card; make sure they are redrawn from
    // scratch once data shows up again.
    for (auto& card : cards_) {
      if (card.drawn || card.blank) card = CardCache{};
    }
    drawEmptyCard();
    tft_.endWrite();
    return;
  }

  if (emptyDrawn_) {
    // Leaving the empty state: the space between the two cards belonged to the
    // single tall card and must go back to the screen background.
    tft_.fillRect(0, kHeaderH, kScreenW, kFooterY - kHeaderH, kBg);
    emptyDrawn_ = false;
    for (auto& card : cards_) card = CardCache{};
  }

  const size_t first = page_ * kServersPerPage;
  for (size_t slot = 0; slot < kServersPerPage; ++slot) {
    const size_t index = first + slot;
    if (index < snapshots.size()) {
      renderCard(snapshots[index], slot, selecting_ && index == selectedIndex_);
    } else {
      CardCache& card = cards_[slot];
      if (!card.blank) {
        tft_.fillRoundRect(kCardX, cardY(slot), kCardW, kCardH, 8, kBg);
        card = CardCache{};
        card.blank = true;
      }
    }
  }

  tft_.endWrite();
}

void TftDashboard::ensureChrome() {
  if (chromeDrawn_) return;

  tft_.fillRect(0, 0, kScreenW, kHeaderH, kPanel);
  tft_.fillRect(0, kFooterY, kScreenW, kScreenH - kFooterY, kPanel);

  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("VPS MONITOR", 8, 7, 2);

  headerRight_.valid = false;
  footerLeft_.valid = false;
  footerPage_.valid = false;
  chromeDrawn_ = true;
}

void TftDashboard::updateHeader() {
  const String ip = WiFi.localIP().toString();
  const bool online = WiFi.status() == WL_CONNECTED && ip != "0.0.0.0";

  drawRightField(headerRight_, online ? ip : String("WiFi offline"), kScreenW - 8, 11,
                 kScreenW - 148, 140, 1, online ? kMuted : kErr);
}

void TftDashboard::updateFooter(size_t pages) {
  const uint16_t color =
      monitor_.isPolling() ? kWarn : (selecting_ ? kSelected : kMuted);
  const String left = ellipsize(tft_, footerText(), 1, 232);
  drawField(footerLeft_, left, 8, kFooterY + 7, 1, color, 8 + 232);

  String label;
  if (pages > 1) label = String(page_ + 1) + "/" + String(pages);
  drawRightField(footerPage_, label, kScreenW - 8, kFooterY + 7, kScreenW - 50, 42, 1,
                 kMuted);
}

void TftDashboard::drawCardChrome(int x, int y, bool selected) {
  tft_.fillRoundRect(x, y, kCardW, kCardH, 8, kPanel);
  tft_.drawRoundRect(x, y, kCardW, kCardH, 8, selected ? kSelected : kLine);
}

void TftDashboard::drawCardBorder(int x, int y, bool selected) {
  tft_.drawRoundRect(x, y, kCardW, kCardH, 8, selected ? kSelected : kLine);
}

void TftDashboard::renderCard(const MonitorService::Snapshot& snapshot, size_t slot,
                              bool selected) {
  CardCache& cache = cards_[slot];
  if (cache.blank) cache = CardCache{};

  const ServerConfig& server = snapshot.server;
  const ServerMetrics& metrics = snapshot.metrics;
  const int x = kCardX;
  const int y = cardY(slot);

  if (!cache.drawn) {
    drawCardChrome(x, y, selected);
    cache = CardCache{};
    cache.drawn = true;
    cache.selected = selected;
  } else if (cache.selected != selected) {
    drawCardBorder(x, y, selected);
    cache.selected = selected;
  }

  // ---- fixed top row -----------------------------------------------------
  const String name = ellipsize(tft_, server.displayName(), 2, 194);
  drawField(cache.name, name, x + 10, y + 6, 2, kText, x + 204);

  uint16_t chipColor = kMuted;
  const char* chip = "WAIT";
  if (!server.enabled) {
    chip = "PAUSED";
  } else if (metrics.ok) {
    chipColor = kOk;
    chip = "OK";
  } else if (metrics.valid) {
    chipColor = kErr;
    chip = "ERROR";
  }
  drawRightField(cache.chip, chip, x + kCardW - 10, y + 6, x + 220, 82, 2, chipColor);

  const String endpoint = ellipsize(tft_, server.endpoint(), 1, kCardW - 20);
  drawField(cache.endpoint, endpoint, x + 10, y + 25, 1, kMuted, x + kCardW - 10);

  // ---- lower body: layout may change between waiting/error/metrics -------
  CardLayout layout = CardLayout::Waiting;
  if (metrics.valid) layout = metrics.ok ? CardLayout::Metrics : CardLayout::Error;

  if (layout != cache.layout) {
    // Different widgets will occupy the body, so clear it once. This only
    // happens on real state changes (first data, error start/end), not on
    // every refresh.
    tft_.fillRect(x + 6, y + 34, kCardW - 12, 50, kPanel);
    cache.layout = layout;
    cache.labelsDrawn = false;
    cache.note.valid = false;
    cache.error.valid = false;
    cache.load.valid = false;
    cache.mem.valid = false;
    cache.temp.valid = false;
    cache.barFilled = -1;
    cache.barColor = 0;
  }

  if (layout == CardLayout::Waiting) {
    const String note = server.enabled ? "waiting for first poll..." : "polling is paused";
    drawField(cache.note, note, x + 10, y + 44, 2, kMuted, x + kCardW - 10);
    return;
  }

  if (layout == CardLayout::Error) {
    if (!cache.error.valid || cache.error.text != metrics.error) {
      // A longer previous message must not leave fragments behind.
      tft_.fillRect(x + 6, y + 34, kCardW - 12, 50, kPanel);
      drawWrapped(tft_, metrics.error, x + 10, y + 42, x + kCardW - 10, 2, kErr, kPanel);
      cache.error.text = metrics.error;
      cache.error.color = kErr;
      cache.error.valid = true;
    }
    return;
  }

  // ---- metrics -----------------------------------------------------------
  if (!cache.labelsDrawn) {
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(kMuted, kPanel);
    tft_.drawString("LOAD", x + 10, y + 38, 1);
    tft_.drawString("MEM", x + 95, y + 38, 1);
    tft_.drawString("TEMP C", x + 240, y + 38, 1);
    cache.labelsDrawn = true;
  }

  String loadText;
  if (isnan(metrics.load1)) {
    loadText = "--";
  } else {
    loadText = metrics.load1 < 10 ? String(metrics.load1, 2) : String(metrics.load1, 1);
  }
  drawField(cache.load, loadText, x + 10, y + 47, 4, kAccent, x + 90);

  if (!isnan(metrics.memUsedMb) && !isnan(metrics.memTotalMb) && metrics.memTotalMb > 0) {
    const float percent = 100.0f * metrics.memUsedMb / metrics.memTotalMb;
    const String memory = String(static_cast<int>(lroundf(metrics.memUsedMb))) + "/" +
                          String(static_cast<int>(lroundf(metrics.memTotalMb))) + " MB";
    drawField(cache.mem, memory, x + 95, y + 51, 2, kText, x + 230);

    int filled = static_cast<int>(kBarW * percent / 100.0f + 0.5f);
    filled = filled < 0 ? 0 : (filled > kBarW ? kBarW : filled);
    const uint16_t color = percent >= 90 ? kErr : kAccent;
    if (filled != cache.barFilled || color != cache.barColor) {
      drawBar(tft_, x + kBarX, y + kBarY, kBarW, kBarH, percent, color);
      cache.barFilled = filled;
      cache.barColor = color;
    }
  } else {
    drawField(cache.mem, "n/a", x + 95, y + 51, 2, kMuted, x + 230);
    if (cache.barFilled >= 0) {
      tft_.fillRect(x + kBarX, y + kBarY, kBarW, kBarH, kPanel);
      cache.barFilled = -1;
      cache.barColor = 0;
    }
  }

  if (!isnan(metrics.tempC)) {
    const uint16_t color =
        metrics.tempC >= 80 ? kErr : (metrics.tempC >= 70 ? kWarn : kText);
    drawField(cache.temp, String(static_cast<int>(lroundf(metrics.tempC))), x + 240,
              y + 47, 4, color, x + kCardW - 8);
  } else {
    // Keep font 4 here so a value <-> "n/a" transition never changes metrics.
    drawField(cache.temp, "n/a", x + 240, y + 47, 4, kMuted, x + kCardW - 8);
  }
}

void TftDashboard::drawEmptyCard() {
  const int x = kCardX;
  const int y = kCardY0;
  const int height = kCardH * static_cast<int>(kServersPerPage) + kCardGap;

  if (!emptyDrawn_) {
    tft_.fillRoundRect(x, y, kCardW, height, 8, kPanel);
    tft_.drawRoundRect(x, y, kCardW, height, 8, kLine);

    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(kText, kPanel);
    tft_.drawString("No servers configured", x + 14, y + 14, 2);

    tft_.setTextColor(kMuted, kPanel);
    tft_.drawString("Add a host in the web interface:", x + 14, y + 44, 1);

    emptyUrl_.valid = false;
    emptyDrawn_ = true;
  }

  const String ip = WiFi.localIP().toString();
  const bool online = WiFi.status() == WL_CONNECTED && ip != "0.0.0.0";

  drawField(emptyUrl_,
            online ? ("http://" + ip + "/servers") : String("connect to Wi-Fi first"),
            x + 14, y + 62, 2, online ? kAccent : kErr, x + kCardW - 14);
}

void TftDashboard::drawField(TextField& field, const String& value, int x, int y,
                             uint8_t font, uint16_t color, int clearToX) {
  if (field.valid && field.text == value && field.color == color) return;

  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(color, kPanel);
  tft_.drawString(value, x, y, font);

  // Opaque text already repainted the glyph cells; only the area to the right
  // of the new value can still hold pixels of a previously longer value.
  const int right = x + tft_.textWidth(value, font);
  if (clearToX > right) {
    tft_.fillRect(right, y, clearToX - right, tft_.fontHeight(font), kPanel);
  }

  field.text = value;
  field.color = color;
  field.valid = true;
}

void TftDashboard::drawRightField(TextField& field, const String& value, int rightX,
                                  int y, int clearX, int clearW, uint8_t font,
                                  uint16_t color) {
  if (field.valid && field.text == value && field.color == color) return;

  // Right-aligned text grows to the left, so shrinking values need the whole
  // field area cleared before the new text is drawn.
  if (field.valid && field.text != value) {
    tft_.fillRect(clearX, y - 2, clearW, tft_.fontHeight(font) + 4, kPanel);
  }

  if (value.length() > 0) {
    tft_.setTextDatum(TR_DATUM);
    tft_.setTextColor(color, kPanel);
    tft_.drawString(value, rightX, y, font);
  }

  field.text = value;
  field.color = color;
  field.valid = true;
}

String TftDashboard::footerText() const {
  if (selecting_) return "A/B: card | C: details";

  String text = "poll " + String(registry_.settings().pollIntervalSec) + "s";

  if (monitor_.isPolling()) return text + " | polling...";

  const uint32_t last = monitor_.lastCycleAtMs();
  if (last == 0) return text + " | waiting for data";

  const uint32_t ageSec = (millis() - last) / 1000;
  if (ageSec < 60) return text + " | " + String(ageSec) + "s ago";
  if (ageSec < 3600) return text + " | " + String(ageSec / 60) + " min ago";
  return text + " | " + String(ageSec / 3600) + " h ago";
}

void TftDashboard::handleButtons(uint32_t now) {
  const uint8_t pressed = buttons_.poll();
  if (pressed == 0) return;

  if (view_ == View::Splash) {
    // A is only special on the boot splash; on the dashboard it stays free.
    if (pressed & ButtonInput::kButtonA) openSetup();
    return;
  }

  if (view_ == View::Setup) {
    if (pressed & ButtonInput::kButtonA) moveSetupSelection(-1);
    if (pressed & ButtonInput::kButtonB) moveSetupSelection(1);
    if (pressed & ButtonInput::kButtonC) closeSetup(now);
    return;
  }

  if (view_ == View::Detail) {
    // C returns to the dashboard, keeping the card selected so the user can
    // continue browsing from where they were.
    if (pressed & ButtonInput::kButtonC) closeDetail(now);
    return;
  }

  if (!selecting_) {
    // A short C press enters Selection mode; A/B do nothing on the dashboard.
    if (pressed & ButtonInput::kButtonC) enterSelection(now);
    return;
  }

  // Any button press counts as activity for the timeout.
  lastSelectionActivityMs_ = now;

  if (pressed & ButtonInput::kButtonA) moveSelection(-1, now);
  if (pressed & ButtonInput::kButtonB) moveSelection(1, now);

  if (selecting_ && (pressed & ButtonInput::kButtonC)) {
    const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
    if (selectedIndex_ < snapshots.size()) {
      openDetail(snapshots[selectedIndex_].server.id);
    }
  }
}

void TftDashboard::openSetup() {
  view_ = View::Setup;
  setupOpen_.store(true);
  setupSelection_ = 0;  // default to OK
  setupFrameDrawn_ = false;
}

void TftDashboard::closeSetup(uint32_t now) {
  // Both OK and Cancel are placeholders for now: report the chosen one and go
  // back to the splash. The splash then yields to the dashboard when boot is
  // complete.
  Serial.println(setupSelection_ == 0 ? F("WiFi setup: OK (placeholder)")
                                      : F("WiFi setup: Cancel"));

  view_ = View::Splash;
  setupOpen_.store(false);
  setupFrameDrawn_ = false;
  splashHoldUntilMs_ = now + kSplashResumeHoldMs;
  drawSplash(wifiSsid_);
}

void TftDashboard::moveSetupSelection(int delta) {
  const int count = static_cast<int>(kSetupChoices);
  int selection = static_cast<int>(setupSelection_) + delta;
  selection = (selection % count + count) % count;
  setupSelection_ = static_cast<uint8_t>(selection);
}

void TftDashboard::enterSelection(uint32_t now) {
  const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
  if (snapshots.empty()) return;

  selecting_ = true;

  // Start with the first card of the page that is currently visible.
  selectedIndex_ = page_ * kServersPerPage;
  if (selectedIndex_ >= snapshots.size()) selectedIndex_ = snapshots.size() - 1;

  lastSelectionActivityMs_ = now;
  lastPageMs_ = now;
  forceRender_ = true;
}

void TftDashboard::moveSelection(int delta, uint32_t now) {
  const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();

  if (snapshots.empty()) {
    exitSelection(now);
    return;
  }

  if (selectedIndex_ >= snapshots.size()) selectedIndex_ = snapshots.size() - 1;

  const size_t count = snapshots.size();
  if (delta < 0) {
    selectedIndex_ = (selectedIndex_ + count - 1) % count;
  } else {
    selectedIndex_ = (selectedIndex_ + 1) % count;
  }

  page_ = selectedIndex_ / kServersPerPage;
  lastSelectionActivityMs_ = now;
  lastPageMs_ = now;
  forceRender_ = true;
}

void TftDashboard::exitSelection(uint32_t now) {
  if (!selecting_) return;

  selecting_ = false;
  lastPageMs_ = now;
  forceRender_ = true;
}

void TftDashboard::openDetail(const String& serverId) {
  view_ = View::Detail;
  detailId_ = serverId;
  resetDetail();
  detail_.frameDrawn = false;
  lastPageMs_ = millis();
}

void TftDashboard::closeDetail(uint32_t now) {
  view_ = View::Dashboard;
  detailId_ = "";
  resetDetail();
  invalidateDashboard();

  // Returning from the detail screen restarts the Selection-mode timeout.
  // Use the caller's timestamp: a later millis() would make the subtraction in
  // update()'s timeout check underflow and exit Selection mode immediately.
  if (selecting_) lastSelectionActivityMs_ = now;
}

void TftDashboard::resetDetail() {
  detail_ = DetailCache{};
}

void TftDashboard::invalidateDashboard() {
  chromeDrawn_ = false;
  emptyDrawn_ = false;
  headerRight_ = TextField{};
  footerLeft_ = TextField{};
  footerPage_ = TextField{};
  emptyUrl_ = TextField{};
  for (auto& card : cards_) card = CardCache{};
  hasRendered_ = false;
  lastPageMs_ = millis();
}

void TftDashboard::drawDetailFrame() {
  tft_.fillScreen(kPanel);
  tft_.fillRect(0, 0, kScreenW, kDetailHeaderH, kBg);

  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("SERVER DETAIL", 8, 5, 2);

  tft_.setTextDatum(TR_DATUM);
  tft_.setTextColor(kMuted, kBg);
  tft_.drawString("C: back", kScreenW - 8, 8, 1);

  tft_.setTextDatum(TL_DATUM);
  tft_.drawFastHLine(10, kDetailDivider1Y, kScreenW - 20, kLine);
  tft_.drawFastHLine(10, kDetailDivider2Y, kScreenW - 20, kLine);

  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString("LOAD 1m", 10, 88, 1);
  tft_.drawString("TEMP C", 250, 88, 1);
  tft_.drawString("MEMORY", 10, 136, 1);
  tft_.drawString("LAST POLL", 10, 182, 1);
  tft_.drawString("TOOK", 205, 182, 1);
}

void TftDashboard::renderDetail(const MonitorService::Snapshot& snapshot) {
  const ServerConfig& server = snapshot.server;
  const ServerMetrics& metrics = snapshot.metrics;

  tft_.startWrite();

  // ---- identity ----------------------------------------------------------
  const String name = ellipsize(tft_, server.displayName(), 4, 185);
  drawField(detail_.name, name, 10, 34, 4, kText, 200);

  uint16_t chipColor = kMuted;
  const char* chip = "WAIT";
  if (!server.enabled) {
    chip = "PAUSED";
  } else if (metrics.ok) {
    chipColor = kOk;
    chip = "OK";
  } else if (metrics.valid) {
    chipColor = kErr;
    chip = "ERROR";
  }
  drawRightField(detail_.chip, chip, kScreenW - 10, 40, 205, 107, 2, chipColor);

  String endpoint = server.user.length() > 0 ? server.user + "@" + server.endpoint()
                                             : server.endpoint();
  endpoint = ellipsize(tft_, endpoint, 1, kScreenW - 20);
  drawField(detail_.endpoint, endpoint, 10, 64, 1, kMuted, kScreenW - 10);

  // ---- load --------------------------------------------------------------
  String loadText;
  if (isnan(metrics.load1)) {
    loadText = "n/a";
  } else {
    loadText = metrics.load1 < 10 ? String(metrics.load1, 2) : String(metrics.load1, 1);
  }
  drawField(detail_.load, loadText, 10, 96, 4, kAccent, 230);

  // ---- temperature -------------------------------------------------------
  if (!isnan(metrics.tempC)) {
    const uint16_t color =
        metrics.tempC >= 80 ? kErr : (metrics.tempC >= 70 ? kWarn : kText);
    drawRightField(detail_.temp,
                   String(static_cast<int>(lroundf(metrics.tempC))) + " C", kScreenW - 10,
                   96, 240, 72, 4, color);
  } else {
    drawRightField(detail_.temp, "n/a", kScreenW - 10, 96, 240, 72, 4, kMuted);
  }

  // ---- memory ------------------------------------------------------------
  if (!isnan(metrics.memUsedMb) && !isnan(metrics.memTotalMb) && metrics.memTotalMb > 0) {
    const float percent = 100.0f * metrics.memUsedMb / metrics.memTotalMb;
    const String memory =
        String(static_cast<int>(lroundf(metrics.memUsedMb))) + "/" +
        String(static_cast<int>(lroundf(metrics.memTotalMb))) + " MB (" +
        String(static_cast<int>(lroundf(percent))) + "%)";
    drawField(detail_.mem, memory, 10, 146, 2, kText, kScreenW - 10);

    int filled = static_cast<int>(kDetailBarW * percent / 100.0f + 0.5f);
    filled = filled < 0 ? 0 : (filled > kDetailBarW ? kDetailBarW : filled);
    const uint16_t color = percent >= 90 ? kErr : kAccent;
    if (filled != detail_.barFilled || color != detail_.barColor) {
      drawBar(tft_, kDetailBarX, kDetailBarY, kDetailBarW, kDetailBarH, percent, color);
      detail_.barFilled = filled;
      detail_.barColor = color;
    }
  } else {
    drawField(detail_.mem, "n/a", 10, 146, 2, kMuted, kScreenW - 10);
    if (detail_.barFilled >= 0) {
      tft_.fillRect(kDetailBarX, kDetailBarY, kDetailBarW, kDetailBarH, kPanel);
      detail_.barFilled = -1;
      detail_.barColor = 0;
    }
  }

  // ---- poll age / duration ----------------------------------------------
  String ageText = "--";
  if (metrics.polledAtMs != 0) {
    const uint32_t ageSec = (millis() - metrics.polledAtMs) / 1000;
    if (ageSec < 60) {
      ageText = String(ageSec) + "s ago";
    } else if (ageSec < 3600) {
      ageText = String(ageSec / 60) + " min ago";
    } else {
      ageText = String(ageSec / 3600) + " h ago";
    }
  }
  drawField(detail_.age, ageText, 80, 182, 1, kMuted, 200);

  String tookText = "--";
  if (metrics.valid) {
    tookText = metrics.durationMs < 1000 ? String(metrics.durationMs) + " ms"
                                         : String(metrics.durationMs / 1000.0f, 1) + " s";
  }
  drawField(detail_.took, tookText, 240, 182, 1, kMuted, kScreenW - 10);

  // ---- waiting / error note ---------------------------------------------
  uint8_t noteKind = 0;
  String noteText;
  if (!server.enabled) {
    noteKind = 1;
    noteText = "polling is paused";
  } else if (!metrics.valid) {
    noteKind = 1;
    noteText = "waiting for first poll...";
  } else if (!metrics.ok) {
    noteKind = 2;
    noteText = metrics.error;
  }

  if (noteKind != detail_.noteKind || detail_.note.text != noteText || !detail_.note.valid) {
    tft_.fillRect(10, kDetailNoteY - 2, kScreenW - 20, kScreenH - kDetailNoteY, kPanel);
    detail_.noteKind = noteKind;
    detail_.note.text = noteText;
    detail_.note.color = noteKind == 2 ? kErr : kMuted;
    detail_.note.valid = true;

    if (noteKind == 2) {
      drawWrapped(tft_, noteText, 10, kDetailNoteY, kScreenW - 10, 2, kErr, kPanel);
    } else if (noteKind == 1) {
      tft_.setTextDatum(TL_DATUM);
      tft_.setTextColor(kMuted, kPanel);
      tft_.drawString(noteText, 10, kDetailNoteY, 1);
    }
  }

  tft_.endWrite();
}

#else  // !VPSPANEL_HAS_DISPLAY

TftDashboard::TftDashboard(MonitorService& monitor, ServerRegistry& registry)
    : monitor_(monitor), registry_(registry) {}

void TftDashboard::begin(const String& wifiSsid) { (void)wifiSsid; }

void TftDashboard::update() {}

void TftDashboard::finishBoot() {}

#endif
