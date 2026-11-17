#pragma once

#include <Arduino.h>

// Persists the Wi-Fi credentials entered on the TFT setup dialog in NVS so the
// station can reconnect after a reboot without reflashing.
class NvsWifiCredentialStore {
 public:
  // Returns false when no credentials have been saved yet, in which case the
  // caller keeps its compile-time defaults.
  bool load(String& ssid, String& password) const;

  bool save(const String& ssid, const String& password);

 private:
  static constexpr const char* kNamespace = "wifi";
  static constexpr const char* kSsidKey = "ssid";
  static constexpr const char* kPasswordKey = "pass";
};
