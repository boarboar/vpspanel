#pragma once

#include <Arduino.h>
#include <cmath>

// One measurement of a monitored host. "NAN" means "not reported by the host".
struct ServerMetrics {
  bool valid = false;    // at least one poll has completed for this server
  bool ok = false;       // the last poll succeeded
  String error;          // human readable reason when ok == false

  float cpuPercent = NAN;  // overall CPU usage
  float load1 = NAN;       // 1 minute load average
  float memUsedMb = NAN;   // used memory
  float memTotalMb = NAN;  // total memory
  float diskUsedMb = NAN;  // used space on the root filesystem
  float diskTotalMb = NAN; // total space on the root filesystem

  uint32_t polledAtMs = 0;  // millis() when the poll finished
  uint32_t durationMs = 0;  // how long the SSH poll took
};
