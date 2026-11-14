#pragma once

#include <Arduino.h>

// A monitored Linux host. This is pure data: no networking, no storage.
struct ServerConfig {
  String id;        // stable identifier generated when the server is added
  String name;      // human readable label (falls back to host)
  String host;      // hostname or IP address
  uint16_t port = 22;
  String user;      // SSH login
  String password;  // SSH password (stored in NVS, see README)
  bool enabled = true;

  String displayName() const {
    return name.length() > 0 ? name : host;
  }

  String endpoint() const {
    return host + ":" + String(port);
  }
};
