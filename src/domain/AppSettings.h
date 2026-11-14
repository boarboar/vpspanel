#pragma once

#include <Arduino.h>

// Settings that apply to the polling loop.
struct AppSettings {
  static constexpr uint16_t kMinPollIntervalSec = 5;
  static constexpr uint16_t kMaxPollIntervalSec = 3600;
  static constexpr uint16_t kMinSshTimeoutSec = 2;
  static constexpr uint16_t kMaxSshTimeoutSec = 60;

  uint16_t pollIntervalSec = 15;
  uint16_t sshTimeoutSec = 10;

  AppSettings sanitized() const {
    AppSettings s = *this;
    if (s.pollIntervalSec < kMinPollIntervalSec) s.pollIntervalSec = kMinPollIntervalSec;
    if (s.pollIntervalSec > kMaxPollIntervalSec) s.pollIntervalSec = kMaxPollIntervalSec;
    if (s.sshTimeoutSec < kMinSshTimeoutSec) s.sshTimeoutSec = kMinSshTimeoutSec;
    if (s.sshTimeoutSec > kMaxSshTimeoutSec) s.sshTimeoutSec = kMaxSshTimeoutSec;
    return s;
  }
};
