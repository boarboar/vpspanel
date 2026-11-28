#pragma once

#include <Arduino.h>
#include <cmath>

// One measurement of a monitored host. "NAN" means "not reported by the host".
struct ServerMetrics {
  bool valid = false;    // at least one poll has completed for this server
  bool ok = false;       // the last poll succeeded
  String error;          // human readable reason when ok == false

  // Consecutive failed SSH polls, reset to 0 by a successful poll. The host is
  // only reported as ERROR once this reaches kErrorAfterFailedAttempts.
  uint8_t consecutiveFailures = 0;

  // True once at least one SSH poll has succeeded. Until then a failed poll is
  // reported as "n/a" instead of a degraded "OK".
  bool everSucceeded = false;

  float cpuPercent = NAN;  // overall CPU usage
  float load1 = NAN;       // 1 minute load average
  float cpuCores = NAN;    // number of logical CPUs
  float memUsedMb = NAN;   // used memory
  float memTotalMb = NAN;  // total memory
  float diskUsedMb = NAN;  // used space on the root filesystem
  float diskTotalMb = NAN; // total space on the root filesystem
  float uptimeSec = NAN;   // seconds since the host booted

  uint32_t polledAtMs = 0;  // millis() when the poll finished
  uint32_t durationMs = 0;  // how long the SSH poll took
};
