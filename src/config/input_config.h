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
// boot, which is what the bootloader expects. Its internal pull-up is weak
// (about 45k); if the GPIO 2 button wire is long or the line is noisy, add an
// external 10k pull-up from GPIO 2 to 3V3.
#define VPSPANEL_BUTTON_A_PIN 0
#define VPSPANEL_BUTTON_B_PIN 1
#define VPSPANEL_BUTTON_C_PIN 2

// The C button on GPIO 2 is unreliable on this build. Swap the B and C roles:
// the reliable GPIO 1 button then performs the C actions (enter Selection mode,
// open details, back, press the setup key) and GPIO 2 only moves the selection.
// Set to 0 to restore the original mapping.
#define VPSPANEL_SWAP_B_C 1

// 1 for buttons that connect the pin to GND (the wiring above), 0 for
// buttons that connect the pin to 3V3.
#define VPSPANEL_BUTTON_ACTIVE_LOW 1

// Leaving Selection mode after this much time without a button press.
#define VPSPANEL_SELECTION_TIMEOUT_MS 30000
