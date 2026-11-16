#pragma once

// ---------------------------------------------------------------------------
// Push-buttons (A / B / C)
// ---------------------------------------------------------------------------
//
// Three momentary buttons wired between the GPIO and GND. The internal
// pull-ups are enabled, so a pressed button reads LOW.
//
//   Button   ESP32-C3
//   A        GPIO 0
//   B        GPIO 1
//   C        GPIO 2
//   common   GND
//
// Set a pin to -1 to disable that button. Note that GPIO 2 is one of the C3
// strapping pins: with the button released the pull-up keeps it high during
// boot, which is what the bootloader expects.
#define VPSPANEL_BUTTON_A_PIN 0
#define VPSPANEL_BUTTON_B_PIN 1
#define VPSPANEL_BUTTON_C_PIN 2

// 1 for buttons that connect the pin to GND (the wiring above), 0 for
// buttons that connect the pin to 3V3.
#define VPSPANEL_BUTTON_ACTIVE_LOW 1

// Leaving Selection mode after this much time without a button press.
#define VPSPANEL_SELECTION_TIMEOUT_MS 30000
