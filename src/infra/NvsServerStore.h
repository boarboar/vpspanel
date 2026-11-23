#pragma once

#include "domain/ServerStore.h"

// Stores the whole configuration as a JSON document in the ESP32 NVS
// partition (flash), namespace "vpspanel".
class NvsServerStore : public ServerStore {
 public:
  bool load(AppSettings& settings, std::vector<ServerConfig>& servers) override;
  bool save(const AppSettings& settings, const std::vector<ServerConfig>& servers) override;

 private:
  static constexpr const char* kNamespace = "vpspanel";
  static constexpr const char* kKey = "config";
  static constexpr uint16_t kSchemaVersion = 2;
};
