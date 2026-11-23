#pragma once

#include <Arduino.h>

// Settings that apply to the two independent polling loops: the SSH-based
// metrics poller and the ICMP ping poller.
struct AppSettings {
  static constexpr uint16_t kMinSshIntervalSec = 5;
  static constexpr uint16_t kMaxSshIntervalSec = 3600;
  static constexpr uint16_t kMinPingIntervalSec = 5;
  static constexpr uint16_t kMaxPingIntervalSec = 3600;
  static constexpr uint16_t kMinSshTimeoutSec = 2;
  static constexpr uint16_t kMaxSshTimeoutSec = 60;

  uint16_t sshIntervalSec = 60;
  uint16_t pingIntervalSec = 15;
  uint16_t sshTimeoutSec = 10;

  AppSettings sanitized() const {
    AppSettings s = *this;
    if (s.sshIntervalSec < kMinSshIntervalSec) s.sshIntervalSec = kMinSshIntervalSec;
    if (s.sshIntervalSec > kMaxSshIntervalSec) s.sshIntervalSec = kMaxSshIntervalSec;
    if (s.pingIntervalSec < kMinPingIntervalSec) s.pingIntervalSec = kMinPingIntervalSec;
    if (s.pingIntervalSec > kMaxPingIntervalSec) s.pingIntervalSec = kMaxPingIntervalSec;
    if (s.sshTimeoutSec < kMinSshTimeoutSec) s.sshTimeoutSec = kMinSshTimeoutSec;
    if (s.sshTimeoutSec > kMaxSshTimeoutSec) s.sshTimeoutSec = kMaxSshTimeoutSec;
    return s;
  }
};
