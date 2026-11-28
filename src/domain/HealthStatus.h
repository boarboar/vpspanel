#pragma once

#include <cstdint>

// A host is reported as ERROR only after this many consecutive failed polls.
// SSH metrics and ICMP pings are counted independently.
constexpr uint8_t kErrorAfterFailedAttempts = 3;

// Coarse health of one poll source (SSH metrics or ICMP ping).
enum class HealthLevel : uint8_t {
  Paused,       // the server is disabled
  Waiting,      // no poll has completed yet
  Unavailable,  // every attempt so far failed, so there is no known-good state
  Ok,           // the latest poll succeeded
  Warn1,        // first consecutive failure, shown as yellow "OK"
  Warn2,        // second consecutive failure, shown as orange "OK"
  Error,        // third (or later) consecutive failure
};

// Maps the outcome of one poll source to a health level. `failures` is the
// number of consecutive failed attempts including the latest one. `everOk`
// tells whether this source has ever produced a successful result: without a
// previous success there is no "OK" to degrade, so the status is "n/a" until
// the error threshold is reached.
inline HealthLevel healthLevel(bool enabled, bool valid, bool ok, bool everOk,
                               uint8_t failures) {
  if (!enabled) return HealthLevel::Paused;
  if (!valid) return HealthLevel::Waiting;
  if (ok) return HealthLevel::Ok;
  if (failures >= kErrorAfterFailedAttempts) return HealthLevel::Error;
  if (!everOk) return HealthLevel::Unavailable;
  if (failures >= 2) return HealthLevel::Warn2;
  return HealthLevel::Warn1;
}
