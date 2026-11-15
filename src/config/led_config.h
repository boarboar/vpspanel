#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Built-in status LED
// ---------------------------------------------------------------------------
//
// Behaviour:
//   2 Hz blink while the Wi-Fi connection is being established
//   2 Hz blink while a polling cycle is running
//   off otherwise
//
// Most ESP32-C3 boards (ESP32-C3 SuperMini and similar) use a plain
// single-colour LED on GPIO 8 wired to 3V3, so the LED lights when the pin is
// driven LOW. That is the default below.
//
// The official ESP32-C3-DevKitM-1 uses an addressable WS2812 RGB LED on GPIO 8
// instead; set VPSPANEL_LED_ADDRESSABLE to 1 for that board. The Arduino core
// then drives it as the virtual LED_BUILTIN pin (white on, off).
//
// Set VPSPANEL_LED_PIN to -1 when the board has no usable built-in LED.

#ifndef VPSPANEL_LED_ADDRESSABLE
#define VPSPANEL_LED_ADDRESSABLE 0
#endif

#ifndef VPSPANEL_LED_PIN
#if VPSPANEL_LED_ADDRESSABLE
#define VPSPANEL_LED_PIN LED_BUILTIN
#else
#define VPSPANEL_LED_PIN 8
#endif
#endif

// Only used for a plain LED: 1 when the LED is wired to GND (lights on HIGH),
// 0 when it is wired to 3V3 (lights on LOW, the common C3 board wiring).
#ifndef VPSPANEL_LED_ACTIVE_HIGH
#define VPSPANEL_LED_ACTIVE_HIGH 0
#endif
