#pragma once

#include <Arduino.h>

// Drives the board's built-in LED as a cheap status indicator that works even
// before the display or Wi-Fi is up:
//
//   * Wi-Fi associating / reconnecting -> 2 Hz blink
//   * polling cycle in progress        -> 2 Hz blink
//   * otherwise                        -> off
//
// Everything is non-blocking: setters only store the desired state, update()
// does the timing and must be called frequently (loop task, Wi-Fi connect
// loop, ...).
class StatusLed {
 public:
  void begin();

  void setWifiConnecting(bool connecting);
  void setPolling(bool polling);

  void update();

 private:
  void apply(bool on);

  bool wifiConnecting_ = false;
  bool polling_ = false;
  bool blinking_ = false;
  bool on_ = false;
  uint32_t lastBlinkMs_ = 0;
};
