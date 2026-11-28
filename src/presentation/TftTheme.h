#pragma once

#include <Arduino.h>

// Palette and panel geometry shared by every TFT screen. The colours are the
// same as the web dashboard (RGB565).
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
constexpr uint16_t kYellow = rgb565(255, 214, 102);  // first failed attempt
constexpr uint16_t kOrange = rgb565(255, 150, 60);   // second failed attempt
constexpr uint16_t kErr = rgb565(255, 107, 107);
constexpr uint16_t kSelected = rgb565(255, 214, 102);  // Selection-mode border

// Landscape orientation: the panel is mounted as 320x240.
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
