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

// ---------------------------------------------------------------------------
// XPT2046 resistive touch controller (shares the display SPI bus)
// ---------------------------------------------------------------------------
//
//   T_CLK            GPIO 4  (same as TFT_SCLK)
//   T_DIN            GPIO 6  (same as TFT_MOSI)
//   T_DO             GPIO 5  (same as TFT_MISO)
//   T_CS             GPIO 1
//   T_IRQ            GPIO 0  (active low, input with pull-up)
//
// The TFT_eSPI library needs -DTOUCH_CS=<pin> in platformio.ini; the
// static_assert in TftDashboard.cpp keeps the two values in sync.
//
// T_IRQ is optional: it is only an extra "something is touching the panel"
// signal, used together with the pressure (Z) reading so a press is detected
// even when the Z measurement is marginal. Set VPSPANEL_TOUCH_IRQ to -1 when
// the pin is not wired.
#define VPSPANEL_TOUCH_CS 1
#define VPSPANEL_TOUCH_IRQ 0

// Fallback calibration used until the panel has been calibrated. These are
// values reported by TFT_eSPI's Touch_calibrate example for a common 2.4"
// ILI9341/XPT2046 module in landscape rotation; the values are {x0, xSpan,
// y0, ySpan, flags}. The fifth field packs the flags:
//   bit 0 rotate, bit 1 invert X, bit 2 invert Y.
//
// Calibration is offered on every boot: the splash screen shows
// "hold screen to calibrate touch" and starts TFT_eSPI's four-corner wizard
// when the panel is held. The result is stored in NVS and used on later boots;
// ignoring the prompt keeps the stored (or these fallback) values.
#define VPSPANEL_TOUCH_CAL_X0 286
#define VPSPANEL_TOUCH_CAL_X1 3534
#define VPSPANEL_TOUCH_CAL_Y0 283
#define VPSPANEL_TOUCH_CAL_Y1 3600
#define VPSPANEL_TOUCH_CAL_FLAGS 6

// Raw pressure (Z) above which a touch is accepted, plus the optional PENIRQ
// line. Lower this if taps are not detected, raise it if the screen reacts to
// phantom touches. The boot log prints the idle value:
//   Touch: idle Z=0 (threshold 200), T_IRQ=1
// so the right threshold can be picked from what the panel actually reports.
#define VPSPANEL_TOUCH_Z_THRESHOLD 200

// Print raw Z / raw coordinates / mapped coordinates on Serial for debugging.
#define VPSPANEL_TOUCH_DEBUG 0
