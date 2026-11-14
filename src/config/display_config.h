#pragma once

// ---------------------------------------------------------------------------
// TFT dashboard (TJCTM24024-SPI / ILI9341 240x320)
// ---------------------------------------------------------------------------
//
// Set VPSPANEL_HAS_DISPLAY to 0 to compile the firmware without the display.
// The display is purely an output device: it reads the same MonitorService
// snapshots as the web dashboard and does not affect polling.

#define VPSPANEL_HAS_DISPLAY 1

// Wiring for the ESP32-C3 DevKitM-1 (change to match your board).
// Avoid GPIO 2, 8 and 9 (strapping pins), GPIO 11-17 (SPI flash) and GPIO
// 18/19 (USB).
//
// These values are mirrored as -DTFT_* flags in platformio.ini. TFT_eSPI is a
// separate library, so it does not see this project's headers; the build flags
// are what the library actually compiles with. Keep both copies in sync -
// TftDashboard.cpp has static_asserts that fail the build on a mismatch.
//
//   TJCTM24024-SPI   ESP32-C3
//   VCC              3V3
//   GND              GND
//   CS               GPIO 7
//   RESET            GPIO 10
//   DC               GPIO 3
//   MOSI             GPIO 6
//   SCK              GPIO 4
//   MISO             GPIO 5
//   LED              see VPSPANEL_TFT_BL below
//
#define VPSPANEL_TFT_MISO 5
#define VPSPANEL_TFT_MOSI 6
#define VPSPANEL_TFT_SCLK 4
#define VPSPANEL_TFT_CS 7
#define VPSPANEL_TFT_DC 3
#define VPSPANEL_TFT_RST 10

// Backlight pin. Use -1 when the module's LED pin is wired to 3V3.
// Only use a GPIO if the module has a backlight transistor; the LED pin of a
// bare module can draw more current than a GPIO should source.
#define VPSPANEL_TFT_BL -1
#define VPSPANEL_TFT_BACKLIGHT_ON 1  // use 0 for an active-low backlight driver

// Landscape orientation: 1 or 3 (use 3 if the picture is upside down).
#define VPSPANEL_TFT_ROTATION 1
