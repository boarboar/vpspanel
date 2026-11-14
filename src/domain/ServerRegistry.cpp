#include "domain/ServerRegistry.h"

#include <algorithm>
#include <cstdio>

#include <esp_random.h>

namespace {

String generateId() {
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return String(buffer);
}

void sanitize(ServerConfig& server) {
  auto clamp = [](String& value, unsigned int maxLength, bool trim = true) {
    if (value.length() > maxLength) value = value.substring(0, maxLength);
    if (trim) value.trim();
  };

  clamp(server.host, 128);
  clamp(server.name, 32);
  clamp(server.user, 64);
  clamp(server.password, 128, false);  // passwords may contain leading/trailing spaces
  if (server.port == 0) server.port = 22;
}

}  // namespace

ServerRegistry::ServerRegistry(ServerStore& store) : store_(store) {}

void ServerRegistry::begin() {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateMutex();
  }

  AppSettings loadedSettings;
  std::vector<ServerConfig> loadedServers;
  loadedFromStore_ = store_.load(loadedSettings, loadedServers);
  if (loadedFromStore_) {
    settings_ = loadedSettings.sanitized();
    servers_.clear();
    servers_.reserve(loadedServers.size());
    for (auto& server : loadedServers) {
      sanitize(server);
      if (server.id.length() == 0) server.id = generateId();
      const bool duplicate =
          std::any_of(servers_.begin(), servers_.end(),
                      [&server](const ServerConfig& other) { return other.id == server.id; });
      if (duplicate || server.host.length() == 0) continue;
      servers_.push_back(server);
      if (servers_.size() >= kMaxServers) break;
    }
  }
}

AppSettings ServerRegistry::settings() const {
  Guard guard(mutex_);
  return settings_;
}

std::vector<ServerConfig> ServerRegistry::servers() const {
  Guard guard(mutex_);
  return servers_;
}

bool ServerRegistry::find(const String& id, ServerConfig& out) const {
  Guard guard(mutex_);
  for (const auto& server : servers_) {
    if (server.id == id) {
      out = server;
      return true;
    }
  }
  return false;
}

String ServerRegistry::upsert(ServerConfig server) {
  sanitize(server);
  if (server.id.length() == 0) server.id = generateId();

  Guard guard(mutex_);

  const std::vector<ServerConfig> backup = servers_;

  auto it = std::find_if(servers_.begin(), servers_.end(),
                         [&server](const ServerConfig& other) { return other.id == server.id; });

  if (it == servers_.end()) {
    if (servers_.size() >= kMaxServers) return String();
    servers_.push_back(server);
  } else {
    *it = server;
  }

  if (!persistLocked()) {
    servers_ = backup;
    return String();
  }
  return server.id;
}

bool ServerRegistry::remove(const String& id) {
  Guard guard(mutex_);

  const std::vector<ServerConfig> backup = servers_;

  auto it = std::remove_if(servers_.begin(), servers_.end(),
                           [&id](const ServerConfig& server) { return server.id == id; });
  if (it == servers_.end()) return false;

  servers_.erase(it, servers_.end());
  if (!persistLocked()) {
    servers_ = backup;
    return false;
  }
  return true;
}

bool ServerRegistry::updateSettings(const AppSettings& settings) {
  Guard guard(mutex_);
  const AppSettings backup = settings_;
  settings_ = settings.sanitized();
  if (!persistLocked()) {
    settings_ = backup;
    return false;
  }
  return true;
}

bool ServerRegistry::persistLocked() {
  return store_.save(settings_, servers_);
}
