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

  // Consecutive failed probes, reset to 0 by a reply. Independent from the SSH
  // counter; the host is only reported as DOWN once this reaches
  // kErrorAfterFailedAttempts.
  uint8_t consecutiveFailures = 0;

  // True once at least one ICMP reply has been received. Until then a failed
  // probe is reported as "n/a" instead of a degraded "OK".
  bool everReachable = false;

  uint32_t polledAtMs = 0;  // millis() when the probe finished
};
