#pragma once

// Copy this file to "wifi_credentials.h" (same directory) and fill in the
// values below. The real file is excluded from git by .gitignore.
//
//   cp src/config/wifi_credentials.example.h src/config/wifi_credentials.h
//
// These values are the fallback for a fresh device. Credentials entered on the
// TFT Wi-Fi setup screen are stored in NVS and take precedence on later boots.

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Hostname advertised over mDNS (reachable as http://<hostname>.local).
#define WIFI_HOSTNAME "vpspanel"
