#pragma once

#include <Arduino.h>
#include <cmath>

// One ICMP reachability probe of a monitored host. Independent from the SSH
// metrics: it is refreshed by its own polling cycle and keeps its own state.
struct PingResult {
  bool valid = false;     // at least one ping poll has completed
  bool reachable = false; // the host answered the last ICMP echo request
  float rttMs = NAN;      // round trip time in milliseconds, NAN when unreachable
  String error;           // human readable reason when the probe failed

  uint32_t polledAtMs = 0;  // millis() when the probe finished
};
