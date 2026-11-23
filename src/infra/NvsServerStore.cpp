#include "infra/NvsServerStore.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <memory>

bool NvsServerStore::load(AppSettings& settings, std::vector<ServerConfig>& servers) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  const size_t length = prefs.getBytesLength(kKey);
  if (length == 0) {
    prefs.end();
    return false;
  }

  std::unique_ptr<char[]> buffer(new char[length + 1]);
  const size_t stored = prefs.getBytes(kKey, buffer.get(), length);
  prefs.end();

  if (stored != length) return false;
  buffer[length] = '\0';

  String payload(buffer.get(), length);

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;

  JsonObject settingsObj = doc["settings"].as<JsonObject>();
  // "pollSec" is the key used before ping polling existed; it still describes
  // the SSH metrics interval, so older saved configurations keep working.
  if (!settingsObj["sshSec"].isNull()) {
    settings.sshIntervalSec = settingsObj["sshSec"].as<uint16_t>();
  } else if (!settingsObj["pollSec"].isNull()) {
    settings.sshIntervalSec = settingsObj["pollSec"].as<uint16_t>();
  }
  if (!settingsObj["pingSec"].isNull()) {
    settings.pingIntervalSec = settingsObj["pingSec"].as<uint16_t>();
  }
  if (!settingsObj["sshTimeoutSec"].isNull()) {
    settings.sshTimeoutSec = settingsObj["sshTimeoutSec"].as<uint16_t>();
  }

  servers.clear();
  for (JsonObject obj : doc["servers"].as<JsonArray>()) {
    ServerConfig server;
    server.id = obj["id"].as<String>();
    server.name = obj["name"].as<String>();
    server.host = obj["host"].as<String>();
    server.port = obj["port"].isNull() ? 22 : obj["port"].as<uint16_t>();
    server.user = obj["user"].as<String>();
    server.password = obj["password"].as<String>();
    server.enabled = obj["enabled"].isNull() ? true : obj["enabled"].as<bool>();
    servers.push_back(server);
  }

  return true;
}

bool NvsServerStore::save(const AppSettings& settings,
                          const std::vector<ServerConfig>& servers) {
  JsonDocument doc;
  doc["version"] = kSchemaVersion;

  JsonObject settingsObj = doc["settings"].to<JsonObject>();
  settingsObj["sshSec"] = settings.sshIntervalSec;
  settingsObj["pingSec"] = settings.pingIntervalSec;
  settingsObj["sshTimeoutSec"] = settings.sshTimeoutSec;

  JsonArray array = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) {
    JsonObject obj = array.add<JsonObject>();
    obj["id"] = server.id;
    obj["name"] = server.name;
    obj["host"] = server.host;
    obj["port"] = server.port;
    obj["user"] = server.user;
    obj["password"] = server.password;
    obj["enabled"] = server.enabled;
  }

  String payload;
  payload.reserve(192 + servers.size() * 160);
  serializeJson(doc, payload);

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  // Stored as a blob: NVS limits single string values to 4000 bytes, while the
  // configuration may grow with the number of servers.
  const size_t written = prefs.putBytes(kKey, payload.c_str(), payload.length());
  prefs.end();

  return written == payload.length();
}
