#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

#include "domain/AppSettings.h"
#include "domain/ServerConfig.h"
#include "domain/ServerStore.h"

// Owns the configurable server list and polling settings. All access is
// thread-safe because the web UI and the polling task run concurrently.
// Every mutation is persisted through ServerStore (flash).
class ServerRegistry {
 public:
  static constexpr size_t kMaxServers = 16;

  explicit ServerRegistry(ServerStore& store);

  void begin();

  // True when a configuration was found in flash at boot.
  bool loadedFromStore() const { return loadedFromStore_; }

  AppSettings settings() const;

  std::vector<ServerConfig> servers() const;
  bool find(const String& id, ServerConfig& out) const;

  // Inserts when server.id is empty, updates otherwise. Returns the id of the
  // stored server, or an empty string when the change could not be persisted.
  String upsert(ServerConfig server);

  bool remove(const String& id);

  bool updateSettings(const AppSettings& settings);

 private:
  struct Guard {
    explicit Guard(SemaphoreHandle_t mutex) : mutex_(mutex) {
      if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~Guard() {
      if (mutex_) xSemaphoreGive(mutex_);
    }
    SemaphoreHandle_t mutex_;
  };

  bool persistLocked();

  mutable SemaphoreHandle_t mutex_ = nullptr;
  ServerStore& store_;
  AppSettings settings_;
  std::vector<ServerConfig> servers_;
  bool loadedFromStore_ = false;
};
