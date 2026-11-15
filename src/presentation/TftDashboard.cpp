#include "presentation/TftDashboard.h"

#include <cmath>

#include "infra/TouchCalibrationStore.h"

#if VPSPANEL_HAS_DISPLAY

#include <WiFi.h>

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
#ifndef TOUCH_CS
#error "TFT_eSPI touch support is disabled: add -DTOUCH_CS=<pin> to platformio.ini"
#endif
static_assert(TOUCH_CS == VPSPANEL_TOUCH_CS,
              "TFT_eSPI touch CS does not match display_config.h");

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
// so redrawing it faster than this is pointless. The touch controller is still
// polled on every update() call (every ~20 ms) by the display task.
constexpr uint32_t kDetailRefreshMs = 250;

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

int cardY(size_t slot) {
  return kCardY0 + static_cast<int>(slot) * (kCardH + kCardGap);
}

// Maps one raw XPT2046 axis onto a screen axis, clamping values so a slightly
// out-of-range calibration can never wrap around to a far corner.
int mapTouchAxis(uint16_t raw, uint16_t origin, uint16_t span, int size) {
  if (span == 0) span = 1;
  const int value =
      (static_cast<int>(raw) - static_cast<int>(origin)) * size / static_cast<int>(span);
  if (value < 0) return 0;
  if (value >= size) return size - 1;
  return value;
}

// The XPT2046 reports 12-bit raw values (0..4095) and a calibration stores the
// raw origin plus the raw span of each axis. A stored calibration that does not
// fit those bounds (for example from an interrupted wizard) maps every tap to
// the same corner, which looks exactly like a dead touch screen, so it is
// rejected in favour of the compile-time fallback.
bool calibrationLooksValid(const uint16_t cal[5]) {
  const uint16_t x0 = cal[0];
  const uint16_t xSpan = cal[1];
  const uint16_t y0 = cal[2];
  const uint16_t ySpan = cal[3];
  if (x0 < 50 || x0 > 4000 || y0 < 50 || y0 > 4000) return false;
  if (xSpan < 400 || xSpan > 4200 || ySpan < 400 || ySpan > 4200) return false;
  return true;
}

// A raw axis value of 0 or 4095 means the controller returned no real sample
// (wrong chip select, missing MISO, controller not powered). A genuine touch
// always lands inside this window.
bool plausibleRaw(uint16_t value) {
  return value > 80 && value < 4050;
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

TftDashboard::TftDashboard(MonitorService& monitor, ServerRegistry& registry,
                           TouchCalibrationStore& touchCalibration)
    : tft_(), touchCalibration_(touchCalibration), monitor_(monitor), registry_(registry) {}

void TftDashboard::begin(const String& wifiSsid) {
  tft_.init();
  tft_.setRotation(VPSPANEL_TFT_ROTATION);

#if VPSPANEL_TOUCH_IRQ >= 0
  // PENIRQ of the XPT2046 is an open-drain, active-low output.
  pinMode(VPSPANEL_TOUCH_IRQ, INPUT_PULLUP);
#endif

  // Use the calibration stored in NVS, or the compile-time fallback when the
  // wizard has never been run or the stored values are unusable.
  if (touchCalibration_.load(touchCal_) && !calibrationLooksValid(touchCal_)) {
    Serial.println(F("Touch: stored calibration is implausible, using defaults"));
    const uint16_t defaults[kTouchCalValues] = {
        VPSPANEL_TOUCH_CAL_X0, VPSPANEL_TOUCH_CAL_X1, VPSPANEL_TOUCH_CAL_Y0,
        VPSPANEL_TOUCH_CAL_Y1, VPSPANEL_TOUCH_CAL_FLAGS};
    for (size_t i = 0; i < kTouchCalValues; ++i) touchCal_[i] = defaults[i];
  }
  tft_.setTouch(touchCal_);

  tft_.fillScreen(kBg);
  drawSplash(wifiSsid);

  // Offer calibration on every boot: holding the screen for a moment starts
  // the wizard, ignoring it keeps the stored/default calibration.
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(kMuted, kBg);
  tft_.drawString("hold screen to calibrate touch", kScreenW / 2, kScreenH - 18, 1);

  const uint32_t started = millis();
  bool touched = false;
  while ((millis() - started) < 2000) {
    if (tft_.getTouchRawZ() >= VPSPANEL_TOUCH_Z_THRESHOLD) {
      touched = true;
      break;
    }
    delay(20);
  }

  if (touched) {
    // calibrateTouch() samples raw positions itself, so it can only be entered
    // when the pressure reading already works; otherwise it would block here
    // forever waiting for a sample that never arrives.
    calibrateTouch();
    tft_.fillScreen(kBg);
    drawSplash(wifiSsid);
  }

  // One diagnostic line per boot: it shows whether the controller answers at
  // all. Z stays at 0 when the SPI/CS wiring to the XPT2046 is broken, while a
  // value clearly above the threshold without a finger on the panel means the
  // threshold is too low.
#if VPSPANEL_TOUCH_IRQ >= 0
  const int irqLevel = digitalRead(VPSPANEL_TOUCH_IRQ);
#else
  const int irqLevel = -1;
#endif
  Serial.printf("Touch: idle Z=%u (threshold %u), T_IRQ=%d\n", tft_.getTouchRawZ(),
                static_cast<unsigned>(VPSPANEL_TOUCH_Z_THRESHOLD), irqLevel);

  initialized_ = true;
  lastPageMs_ = millis();
}

void TftDashboard::calibrateTouch() {
  tft_.calibrateTouch(touchCal_, kAccent, kBg, 12);

  if (touchCalibration_.save(touchCal_)) {
    Serial.println(F("Touch: calibration saved"));
  } else {
    Serial.println(F("Touch: calibration could not be saved"));
  }
  tft_.setTouch(touchCal_);
}

void TftDashboard::update() {
  if (!initialized_) return;

  const uint32_t now = millis();

  // Touch handling is edge triggered: a press on a dashboard card opens that
  // server's detail screen, while a press on the detail screen (or anywhere
  // else) returns to the dashboard.
  handleTouch();

  if (view_ == View::Detail) {
    // update() now runs every ~20 ms so taps are sampled often enough; the
    // detail values themselves only need an occasional refresh.
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
      closeDetail();
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

  if (hasRendered_ && !newCycle && (now - lastRenderMs_) < kRefreshMs) return;
  lastCycleSeenMs_ = cycle;

  const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
  size_t pages = snapshots.empty()
                     ? 1
                     : (snapshots.size() + kServersPerPage - 1) / kServersPerPage;

  if (pages > 1 && hasRendered_ && (now - lastPageMs_) >= kPageMs) {
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
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("VPS MONITOR", kScreenW / 2, kScreenH / 2 - 40, 4);

  // Tell the user which network the device is about to join; the panel stays
  // on this screen until the first dashboard render.
  const String label = "Wi-Fi: " + wifiSsid;
  tft_.setTextColor(kText, kBg);
  tft_.drawString(ellipsize(tft_, label, 2, kScreenW - 20), kScreenW / 2,
                  kScreenH / 2 + 6, 2);

  tft_.setTextColor(kMuted, kBg);
  tft_.drawString("connecting...", kScreenW / 2, kScreenH / 2 + 32, 1);
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
      renderCard(snapshots[index], slot);
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
  const String left = ellipsize(tft_, footerText(), 1, 232);
  drawField(footerLeft_, left, 8, kFooterY + 7, 1,
            monitor_.isPolling() ? kWarn : kMuted, 8 + 232);

  String label;
  if (pages > 1) label = String(page_ + 1) + "/" + String(pages);
  drawRightField(footerPage_, label, kScreenW - 8, kFooterY + 7, kScreenW - 50, 42, 1,
                 kMuted);
}

void TftDashboard::drawCardChrome(int x, int y) {
  tft_.fillRoundRect(x, y, kCardW, kCardH, 8, kPanel);
  tft_.drawRoundRect(x, y, kCardW, kCardH, 8, kLine);
}

void TftDashboard::renderCard(const MonitorService::Snapshot& snapshot, size_t slot) {
  CardCache& cache = cards_[slot];
  if (cache.blank) cache = CardCache{};

  const ServerConfig& server = snapshot.server;
  const ServerMetrics& metrics = snapshot.metrics;
  const int x = kCardX;
  const int y = cardY(slot);

  if (!cache.drawn) {
    drawCardChrome(x, y);
    cache = CardCache{};
    cache.drawn = true;
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
  String text = "poll " + String(registry_.settings().pollIntervalSec) + "s";

  if (monitor_.isPolling()) return text + " | polling...";

  const uint32_t last = monitor_.lastCycleAtMs();
  if (last == 0) return text + " | waiting for data";

  const uint32_t ageSec = (millis() - last) / 1000;
  if (ageSec < 60) return text + " | " + String(ageSec) + "s ago";
  if (ageSec < 3600) return text + " | " + String(ageSec / 60) + " min ago";
  return text + " | " + String(ageSec / 3600) + " h ago";
}

bool TftDashboard::touchPressedSignal(uint16_t z) const {
  if (z >= VPSPANEL_TOUCH_Z_THRESHOLD) return true;

#if VPSPANEL_TOUCH_IRQ >= 0
  // PENIRQ only says "the panel is being touched"; the position still comes
  // from the SPI read. It is used as a second opinion so a marginal pressure
  // reading does not swallow the press.
  return digitalRead(VPSPANEL_TOUCH_IRQ) == LOW;
#else
  return false;
#endif
}

// Second, independent attempt at reading the controller, driven through the
// Arduino SPI driver instead of TFT_eSPI's register-level touch helpers. It is
// only used when a touch is definitely happening (PENIRQ is low) but the
// library's sample was unusable, so it costs nothing in the normal case.
bool TftDashboard::readTouchDirect(uint16_t& rawX, uint16_t& rawY) {
  SPIClass& spi = TFT_eSPI::getSPIinstance();

  spi.beginTransaction(SPISettings(SPI_TOUCH_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(VPSPANEL_TOUCH_CS, LOW);

  // Two conversions per axis: the first one settles the panel after the
  // channel switch, the second is the value that is kept.
  auto readAxis = [&spi](uint8_t command) {
    spi.transfer(command);
    (void)spi.transfer16(0);
    spi.transfer(command);
    return static_cast<uint16_t>((spi.transfer16(0) >> 3) & 0x0FFF);
  };

  const uint16_t y = readAxis(0xD0);  // Y position
  const uint16_t x = readAxis(0x90);  // X position

  digitalWrite(VPSPANEL_TOUCH_CS, HIGH);
  spi.endTransaction();

  rawX = x;
  rawY = y;
  return plausibleRaw(rawX) && plausibleRaw(rawY);
}

bool TftDashboard::readTouch(uint16_t& x, uint16_t& y) {
  const uint16_t z = tft_.getTouchRawZ();
  bool pressed = false;
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  int sx = 0;
  int sy = 0;

  if (touchPressedSignal(z)) {
    tft_.getTouchRaw(&rawX, &rawY);
    bool usable = plausibleRaw(rawX) && plausibleRaw(rawY);

#if VPSPANEL_TOUCH_IRQ >= 0
    if (!usable) usable = readTouchDirect(rawX, rawY);
#endif

    pressed = usable;

    if (pressed) {
      const uint16_t x0 = touchCal_[0];
      const uint16_t xSpan = touchCal_[1];
      const uint16_t y0 = touchCal_[2];
      const uint16_t ySpan = touchCal_[3];
      const uint8_t flags = static_cast<uint8_t>(touchCal_[4]);

      // Same axis convention as TFT_eSPI::convertRawXY, but clamped: with a
      // rough calibration the library rejects out-of-range coordinates, which
      // makes the whole screen appear dead instead of merely inaccurate.
      if (flags & 0x01) {  // touch axes swapped relative to the display axes
        sx = mapTouchAxis(rawY, x0, xSpan, kScreenW);
        sy = mapTouchAxis(rawX, y0, ySpan, kScreenH);
      } else {
        sx = mapTouchAxis(rawX, x0, xSpan, kScreenW);
        sy = mapTouchAxis(rawY, y0, ySpan, kScreenH);
      }
      if (flags & 0x02) sx = kScreenW - 1 - sx;
      if (flags & 0x04) sy = kScreenH - 1 - sy;

      x = static_cast<uint16_t>(sx);
      y = static_cast<uint16_t>(sy);
    }
  }

#if VPSPANEL_TOUCH_DEBUG
  const uint32_t now = millis();
  if (pressed != touchDebugPressed_ || (now - lastTouchDebugMs_) >= 1000) {
    lastTouchDebugMs_ = now;
    touchDebugPressed_ = pressed;
#if VPSPANEL_TOUCH_IRQ >= 0
    const int irqLevel = digitalRead(VPSPANEL_TOUCH_IRQ);
#else
    const int irqLevel = -1;
#endif
    Serial.printf("Touch: z=%u irq=%d raw=%u,%u -> %u,%u pressed=%d\n", z, irqLevel,
                  rawX, rawY, x, y, pressed ? 1 : 0);
  }
#endif

  return pressed;
}

void TftDashboard::handleTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool pressed = readTouch(x, y);

  // Only react to state changes so holding a finger down does not repeatedly
  // open/close the detail screen.
  if (pressed == touchDown_) return;
  touchDown_ = pressed;
  if (!pressed) return;

  // Ignore contact bounce: a press is only accepted after a short quiet
  // period, so one physical tap cannot open and immediately close the detail
  // screen.
  const uint32_t now = millis();
  if ((now - lastTouchMs_) < 250) return;
  lastTouchMs_ = now;

  if (view_ == View::Detail) {
    closeDetail();
    return;
  }

  // The coordinates refer to what is currently visible, i.e. the page that was
  // last rendered, so use the current page_ index as-is.
  const std::vector<MonitorService::Snapshot> snapshots = monitor_.snapshots();
  const size_t first = page_ * kServersPerPage;
  for (size_t slot = 0; slot < kServersPerPage; ++slot) {
    const size_t index = first + slot;
    if (index >= snapshots.size()) break;

    const int left = kCardX;
    const int top = cardY(slot);
    if (x >= left && x < left + kCardW && y >= top && y < top + kCardH) {
      openDetail(snapshots[index].server.id);
      return;
    }
  }
}

void TftDashboard::openDetail(const String& serverId) {
  view_ = View::Detail;
  detailId_ = serverId;
  resetDetail();
  detail_.frameDrawn = false;
  lastPageMs_ = millis();
}

void TftDashboard::closeDetail() {
  view_ = View::Dashboard;
  detailId_ = "";
  resetDetail();
  invalidateDashboard();
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
  tft_.drawString("tap to return", kScreenW - 8, 8, 1);

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

TftDashboard::TftDashboard(MonitorService& monitor, ServerRegistry& registry,
                           TouchCalibrationStore& touchCalibration)
    : monitor_(monitor), registry_(registry) {
  (void)touchCalibration;
}

void TftDashboard::begin(const String& wifiSsid) { (void)wifiSsid; }

void TftDashboard::update() {}

#endif
