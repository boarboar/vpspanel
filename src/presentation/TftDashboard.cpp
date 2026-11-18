#include "presentation/TftDashboard.h"

#include <cmath>

#if VPSPANEL_HAS_DISPLAY

#include <WiFi.h>

#include "config/input_config.h"
#include "presentation/TftTheme.h"

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

constexpr int kHeaderH = 30;
constexpr int kFooterY = 218;

constexpr int kCardX = 5;
constexpr int kCardW = 310;
constexpr int kCardH = 52;
constexpr int kCardGap = 9;
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

// Detail screen layout.
constexpr int kDetailHeaderH = 28;
constexpr int kDetailDivider1Y = 80;
constexpr int kDetailDivider2Y = 130;
constexpr int kDetailBarX = 10;
constexpr int kDetailBarY = 166;
constexpr int kDetailBarW = 300;
constexpr int kDetailBarH = 6;
constexpr int kDetailNoteY = 196;

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

float usagePercent(float used, float total) {
  if (isnan(used) || isnan(total) || total <= 0) return NAN;
  return 100.0f * used / total;
}

String percentText(float percent) {
  if (isnan(percent)) return String("n/a");
  return String(static_cast<int>(lroundf(percent))) + "%";
}

}  // namespace

TftDashboard::TftDashboard(MonitorService& monitor, ServerRegistry& registry)
    : tft_(), monitor_(monitor), registry_(registry), setupDialog_(tft_) {}

void TftDashboard::begin(const String& wifiSsid, const String& wifiPassword) {
  tft_.init();
  tft_.setRotation(VPSPANEL_TFT_ROTATION);

  buttons_.begin();

  wifiSsid_ = wifiSsid;
  wifiPassword_ = wifiPassword;
  view_ = View::Splash;
  setupDialog_.close();

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

  // The setup dialog has no live data; it redraws itself only when its
  // selection changes.
  if (view_ == View::Setup) {
    setupDialog_.update();
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

  // ---- fixed top row: name followed by "host:port" in brackets ------------
  const String suffix = " (" + server.endpoint() + ")";
  const int suffixWidth = tft_.textWidth(suffix, 2);
  String title;
  if (suffixWidth < 194) {
    title = ellipsize(tft_, server.displayName(), 2, 194 - suffixWidth) + suffix;
  } else {
    // Pathological host name: keep the endpoint readable instead.
    title = ellipsize(tft_, server.endpoint(), 2, 194);
  }
  drawField(cache.name, title, x + 10, y + 6, 2, kText, x + 204);

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

  // ---- body: layout may change between waiting/error/metrics -------------
  CardLayout layout = CardLayout::Waiting;
  if (metrics.valid) layout = metrics.ok ? CardLayout::Metrics : CardLayout::Error;

  if (layout != cache.layout) {
    // Different widgets will occupy the body, so clear it once. This only
    // happens on real state changes (first data, error start/end), not on
    // every refresh.
    tft_.fillRect(x + 6, y + 24, kCardW - 12, kCardH - 30, kPanel);
    cache.layout = layout;
    cache.note.valid = false;
    cache.metrics.valid = false;
    cache.error.valid = false;
  }

  if (layout == CardLayout::Waiting) {
    const String note = server.enabled ? "waiting for first poll..." : "polling is paused";
    drawField(cache.note, note, x + 10, y + 27, 2, kMuted, x + kCardW - 10);
    return;
  }

  if (layout == CardLayout::Error) {
    if (!cache.error.valid || cache.error.text != metrics.error) {
      // A longer previous message must not leave fragments behind.
      tft_.fillRect(x + 6, y + 24, kCardW - 12, kCardH - 30, kPanel);
      drawWrapped(tft_, metrics.error, x + 10, y + 27, x + kCardW - 10, 2, kErr, kPanel);
      cache.error.text = metrics.error;
      cache.error.color = kErr;
      cache.error.valid = true;
    }
    return;
  }

  // ---- all metrics on one line -------------------------------------------
  const String line =
      "CPU: " + percentText(metrics.cpuPercent) + "  MEM: " +
      percentText(usagePercent(metrics.memUsedMb, metrics.memTotalMb)) + "  DISK: " +
      percentText(usagePercent(metrics.diskUsedMb, metrics.diskTotalMb));
  drawField(cache.metrics, line, x + 10, y + 27, 2, kText, x + kCardW - 10);
}

void TftDashboard::drawEmptyCard() {
  const int x = kCardX;
  const int y = kCardY0;
  const int height =
      kCardH * static_cast<int>(kServersPerPage) + kCardGap * (static_cast<int>(kServersPerPage) - 1);

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
    const WifiSetupDialog::Result result = setupDialog_.handleButtons(pressed);
    if (result != WifiSetupDialog::Result::None) {
      closeSetup(now, result == WifiSetupDialog::Result::Ok);
    }
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
  setupDialog_.open(wifiSsid_, wifiPassword_);
}

void TftDashboard::closeSetup(uint32_t now, bool ok) {
  // OK applies the entered credentials; Cancel keeps the current ones. Either
  // way the splash is shown again until the boot-time attempt finishes.
  if (ok) {
    wifiSsid_ = setupDialog_.ssid();
    wifiPassword_ = setupDialog_.password();
  }

  Serial.println(ok ? F("WiFi setup: credentials submitted") : F("WiFi setup: cancelled"));

  setupDialog_.close();
  view_ = View::Splash;
  splashHoldUntilMs_ = now + kSplashResumeHoldMs;
  drawSplash(wifiSsid_);
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
  tft_.drawString("CPU %", 10, 88, 1);
  tft_.drawString("LOAD 1m", 120, 88, 1);
  tft_.drawString("DISK %", 250, 88, 1);
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

  // ---- CPU usage ---------------------------------------------------------
  if (!isnan(metrics.cpuPercent)) {
    drawField(detail_.cpu, String(static_cast<int>(lroundf(metrics.cpuPercent))), 10, 96,
              4, kAccent, 110);
  } else {
    drawField(detail_.cpu, "n/a", 10, 96, 4, kMuted, 110);
  }

  // ---- load --------------------------------------------------------------
  String loadText;
  if (isnan(metrics.load1)) {
    loadText = "n/a";
  } else {
    loadText = metrics.load1 < 10 ? String(metrics.load1, 2) : String(metrics.load1, 1);
  }
  drawField(detail_.load, loadText, 120, 96, 4, kAccent, 235);

  // ---- root filesystem usage ---------------------------------------------
  if (!isnan(metrics.diskUsedMb) && !isnan(metrics.diskTotalMb) && metrics.diskTotalMb > 0) {
    const float percent = 100.0f * metrics.diskUsedMb / metrics.diskTotalMb;
    const uint16_t color = percent >= 90 ? kErr : (percent >= 75 ? kWarn : kText);
    drawRightField(detail_.disk, String(static_cast<int>(lroundf(percent))), kScreenW - 10,
                   96, 240, 72, 4, color);
  } else {
    drawRightField(detail_.disk, "n/a", kScreenW - 10, 96, 240, 72, 4, kMuted);
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

void TftDashboard::begin(const String& wifiSsid, const String& wifiPassword) {
  (void)wifiSsid;
  (void)wifiPassword;
}

void TftDashboard::update() {}

void TftDashboard::finishBoot() {}

#endif

bool TftDashboard::isSetupOpen() const { return setupDialog_.isOpen(); }

bool TftDashboard::takeWifiCredentials(String& ssid, String& password) {
  return setupDialog_.takeAcceptedCredentials(ssid, password);
}
