#include "presentation/WebDashboard.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

#include "presentation/WebAssets.h"

namespace {

String htmlEscape(const String& input) {
  String output;
  output.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': output += F("&amp;"); break;
      case '<': output += F("&lt;"); break;
      case '>': output += F("&gt;"); break;
      case '"': output += F("&quot;"); break;
      case '\'': output += F("&#39;"); break;
      default: output += input[i]; break;
    }
  }
  return output;
}

String urlEncode(const String& input) {
  static const char kHex[] = "0123456789ABCDEF";
  String output;
  output.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      output += static_cast<char>(c);
    } else if (c == ' ') {
      output += '+';
    } else {
      output += '%';
      output += kHex[c >> 4];
      output += kHex[c & 0x0F];
    }
  }
  return output;
}

String textField(const char* label, const char* name, const String& value,
                 const char* placeholder = "", const char* type = "text",
                 const char* attrs = "") {
  String html;
  html.reserve(240);
  html += F("<label>");
  html += label;
  html += F("<input type=\"");
  html += type;
  html += F("\" name=\"");
  html += name;
  html += F("\" value=\"");
  html += htmlEscape(value);
  html += F("\" placeholder=\"");
  html += placeholder;
  html += '"';
  if (attrs != nullptr && strlen(attrs) > 0) {
    html += ' ';
    html += attrs;
  }
  html += F("></label>");
  return html;
}

String enabledSelect(bool enabled) {
  String html = F("<label>State<select name=\"enabled\">");
  if (enabled) {
    html += F("<option value=\"1\" selected>Enabled</option>");
    html += F("<option value=\"0\">Paused</option>");
  } else {
    html += F("<option value=\"1\">Enabled</option>");
    html += F("<option value=\"0\" selected>Paused</option>");
  }
  html += F("</select></label>");
  return html;
}

}  // namespace

WebDashboard::WebDashboard(ServerRegistry& registry, MonitorService& monitor, uint16_t port)
    : server_(port), registry_(registry), monitor_(monitor) {}

void WebDashboard::begin() {
  server_.on("/", HTTP_GET, [this]() { handleDashboard(); });
  server_.on("/servers", HTTP_GET, [this]() { handleServersPage(); });
  server_.on("/servers/edit", HTTP_GET, [this]() { handleServersPage(); });
  server_.on("/servers/save", HTTP_POST, [this]() { handleSaveServer(); });
  server_.on("/servers/delete", HTTP_POST, [this]() { handleDeleteServer(); });
  server_.on("/settings", HTTP_POST, [this]() { handleSaveSettings(); });
  server_.on("/api/metrics", HTTP_GET, [this]() { handleApiMetrics(); });
  server_.on("/api/poll", HTTP_POST, [this]() { handleApiPoll(); });
  server_.on("/favicon.ico", HTTP_GET, [this]() { server_.send(204, "text/plain", ""); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();
}

void WebDashboard::handle() {
  server_.handleClient();
}

void WebDashboard::suspend() {
  server_.stop();
}

void WebDashboard::resume() {
  server_.begin();
}

String WebDashboard::renderPage(const String& title, const String& activeNav,
                                const String& content) const {
  String html;
  html.reserve(content.length() + strlen(kCss) + 512);
  html += F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>");
  html += title;
  html += F("</title><style>");
  html += kCss;
  html += F("</style></head><body><header><div class=\"brand\">VPS<span>Monitor</span></div><nav>");
  if (activeNav == "dashboard") {
    html += F("<a class=\"active\" href=\"/\">Dashboard</a><a href=\"/servers\">Servers</a>");
  } else {
    html += F("<a href=\"/\">Dashboard</a><a class=\"active\" href=\"/servers\">Servers</a>");
  }
  html += F("</nav></header><main>");
  html += content;
  html += F("</main></body></html>");
  return html;
}

void WebDashboard::redirectTo(const String& location) {
  server_.sendHeader("Location", location, true);
  server_.send(303, "text/plain", "");
}

void WebDashboard::handleDashboard() {
  server_.send(200, "text/html; charset=utf-8",
               renderPage(F("Dashboard"), F("dashboard"), String(kDashboardHtml)));
}

void WebDashboard::handleServersPage() {
  const std::vector<ServerConfig> servers = registry_.servers();
  const AppSettings settings = registry_.settings();

  String content;
  content.reserve(4600);

  const String message = server_.arg("msg");
  if (message.length() > 0) {
    content += F("<div class=\"msg\">");
    content += htmlEscape(message);
    content += F("</div>");
  }

  content += F("<h2>Servers</h2><div class=\"card\">");
  if (servers.empty()) {
    content += F("<div class=\"empty\">No servers yet. Add one below.</div>");
  } else {
    content += F("<table><thead><tr><th>Server</th><th>User</th><th>State</th>"
                 "<th>Actions</th></tr></thead><tbody>");
    for (const auto& server : servers) {
      content += F("<tr><td><div class=\"mono\">");
      content += htmlEscape(server.displayName());
      content += F("</div><div class=\"muted mono\">");
      content += htmlEscape(server.endpoint());
      content += F("</div></td><td class=\"mono\">");
      content += htmlEscape(server.user);
      content += F("</td><td><span class=\"chip ");
      content += server.enabled ? F("ok\">Enabled") : F("idle\">Paused");
      content += F("</span></td><td><div class=\"actions\"><a href=\"/servers/edit?id=");
      content += urlEncode(server.id);
      content += F("\">Edit</a>"
                   "<form method=\"post\" action=\"/servers/delete\" "
                   "onsubmit=\"return confirm('Delete this server?');\">"
                   "<input type=\"hidden\" name=\"id\" value=\"");
      content += htmlEscape(server.id);
      content += F("\"><button class=\"danger\" type=\"submit\">Delete</button></form>"
                   "</div></td></tr>");
    }
    content += F("</tbody></table>");
  }
  content += F("</div>");

  const String editId = server_.arg("id");
  ServerConfig editing;
  bool isEdit = editId.length() > 0 && registry_.find(editId, editing);
  if (editId.length() > 0 && !isEdit) {
    content += F("<div class=\"msg\">That server no longer exists.</div>");
  }

  content += isEdit ? F("<h2>Edit server</h2>") : F("<h2>Add server</h2>");
  content += F("<form class=\"card form\" method=\"post\" action=\"/servers/save\">");
  if (isEdit) {
    content += F("<input type=\"hidden\" name=\"id\" value=\"");
    content += htmlEscape(editing.id);
    content += F("\">");
  }
  content += textField("Name (optional)", "name", editing.name, "vps-1", "text",
                       "maxlength=\"32\"");
  content += textField("Host", "host", editing.host, "192.168.1.10", "text",
                       "maxlength=\"128\" required");
  content += textField("Port", "port", String(isEdit ? editing.port : 22), "22", "number",
                       "min=\"1\" max=\"65535\" required");
  content += textField("SSH user", "user", editing.user, "root", "text",
                       "maxlength=\"64\" required");
  content += textField("SSH password", "password", "", isEdit ? "(unchanged)" : "", "password",
                       "maxlength=\"128\" autocomplete=\"new-password\"");
  content += enabledSelect(isEdit ? editing.enabled : true);
  content += F("<div class=\"form-actions\"><button type=\"submit\">");
  content += isEdit ? F("Save changes") : F("Add server");
  content += F("</button>");
  if (isEdit) {
    content += F("<a href=\"/servers\">Cancel</a>");
  }
  content += F("</div></form>");

  content += F("<h2>Polling</h2>");
  content += F("<form class=\"card form\" method=\"post\" action=\"/settings\">");
  content += textField("SSH poll interval (seconds)", "sshSec", String(settings.sshIntervalSec), "",
                       "number", "min=\"5\" max=\"3600\" required");
  content += textField("Ping interval (seconds)", "pingSec", String(settings.pingIntervalSec), "",
                       "number", "min=\"5\" max=\"3600\" required");
  content += textField("SSH timeout (seconds)", "timeoutSec", String(settings.sshTimeoutSec), "",
                       "number", "min=\"2\" max=\"60\" required");
  content += F("<div class=\"form-actions\"><button type=\"submit\">Save settings</button>"
               "<span class=\"muted\">The two intervals are independent and apply from the "
               "next cycle.</span></div></form>");

  server_.send(200, "text/html; charset=utf-8",
               renderPage(F("Servers"), F("servers"), content));
}

void WebDashboard::handleSaveServer() {
  ServerConfig server;
  server.id = server_.arg("id");
  server.name = server_.arg("name");
  server.host = server_.arg("host");
  server.user = server_.arg("user");
  server.password = server_.arg("password");
  server.enabled = server_.arg("enabled") != "0";

  const long port = server_.arg("port").toInt();

  String error;
  if (server.host.length() == 0) {
    error = "Host is required";
  } else if (server.user.length() == 0) {
    error = "SSH user is required";
  } else if (port < 1 || port > 65535) {
    error = "Port must be between 1 and 65535";
  } else {
    server.port = static_cast<uint16_t>(port);
  }

  // On edit, an empty password field means "keep the stored password".
  if (error.length() == 0 && server.id.length() > 0) {
    ServerConfig existing;
    if (!registry_.find(server.id, existing)) {
      error = "Server not found";
    } else if (server.password.length() == 0) {
      server.password = existing.password;
    }
  }
  if (error.length() == 0 && server.id.length() == 0 && server.password.length() == 0) {
    error = "SSH password is required";
  }

  if (error.length() == 0 && registry_.upsert(server).length() == 0) {
    error = "Could not save the server (limit is 16 servers, or the flash write failed)";
  } else if (error.length() == 0) {
    monitor_.requestPoll();
    monitor_.requestPing();
  }

  redirectTo(String("/servers?msg=") +
             urlEncode(error.length() > 0 ? error : "Configuration saved"));
}

void WebDashboard::handleDeleteServer() {
  const String id = server_.arg("id");
  ServerConfig existing;
  String error;

  if (id.length() == 0) {
    error = "Missing server id";
  } else if (!registry_.find(id, existing)) {
    error = "Server not found";
  } else if (!registry_.remove(id)) {
    error = "Could not delete the server (flash write failed)";
  }

  redirectTo(String("/servers?msg=") + urlEncode(error.length() > 0 ? error : "Server deleted"));
}

void WebDashboard::handleSaveSettings() {
  const long sshSec = server_.arg("sshSec").toInt();
  const long pingSec = server_.arg("pingSec").toInt();
  const long timeoutSec = server_.arg("timeoutSec").toInt();

  String error;
  if (sshSec < AppSettings::kMinSshIntervalSec || sshSec > AppSettings::kMaxSshIntervalSec) {
    error = "SSH poll interval must be between 5 and 3600 seconds";
  } else if (pingSec < AppSettings::kMinPingIntervalSec ||
             pingSec > AppSettings::kMaxPingIntervalSec) {
    error = "Ping interval must be between 5 and 3600 seconds";
  } else if (timeoutSec < AppSettings::kMinSshTimeoutSec ||
             timeoutSec > AppSettings::kMaxSshTimeoutSec) {
    error = "SSH timeout must be between 2 and 60 seconds";
  } else {
    AppSettings settings;
    settings.sshIntervalSec = static_cast<uint16_t>(sshSec);
    settings.pingIntervalSec = static_cast<uint16_t>(pingSec);
    settings.sshTimeoutSec = static_cast<uint16_t>(timeoutSec);
    if (!registry_.updateSettings(settings)) {
      error = "Could not save the settings (flash write failed)";
    } else {
      monitor_.requestPoll();
      monitor_.requestPing();
    }
  }

  redirectTo(String("/servers?msg=") + urlEncode(error.length() > 0 ? error : "Settings saved"));
}

void WebDashboard::handleApiMetrics() {
  const AppSettings settings = registry_.settings();
  const uint32_t now = millis();
  const uint32_t lastCycleAt = monitor_.lastCycleAtMs();

  JsonDocument doc;
  doc["intervalSec"] = settings.sshIntervalSec;
  doc["sshIntervalSec"] = settings.sshIntervalSec;
  doc["pingIntervalSec"] = settings.pingIntervalSec;
  doc["sshTimeoutSec"] = settings.sshTimeoutSec;
  doc["polling"] = monitor_.isPolling();
  doc["pinging"] = monitor_.isPinging();
  doc["lastCycleAgeMs"] = lastCycleAt == 0 ? -1 : static_cast<int32_t>(now - lastCycleAt);
  const uint32_t lastPingAt = monitor_.lastPingCycleAtMs();
  doc["lastPingAgeMs"] = lastPingAt == 0 ? -1 : static_cast<int32_t>(now - lastPingAt);

  JsonArray array = doc["servers"].to<JsonArray>();
  for (const auto& snapshot : monitor_.snapshots()) {
    const ServerMetrics& metrics = snapshot.metrics;
    const PingResult& ping = snapshot.ping;
    JsonObject obj = array.add<JsonObject>();
    obj["id"] = snapshot.server.id;
    obj["name"] = snapshot.server.displayName();
    obj["host"] = snapshot.server.host;
    obj["port"] = snapshot.server.port;
    obj["enabled"] = snapshot.server.enabled;
    obj["ok"] = metrics.ok;
    obj["error"] = metrics.error;
    obj["load"] = metrics.load1;
    obj["cpu"] = metrics.cpuPercent;
    obj["memUsed"] = metrics.memUsedMb;
    obj["memTotal"] = metrics.memTotalMb;
    obj["diskUsed"] = metrics.diskUsedMb;
    obj["diskTotal"] = metrics.diskTotalMb;
    obj["ageMs"] = metrics.valid ? static_cast<int32_t>(now - metrics.polledAtMs) : -1;
    obj["durationMs"] = metrics.durationMs;
    obj["pingOk"] = ping.valid && ping.reachable;
    obj["pingMs"] = ping.rttMs;
    obj["pingError"] = ping.error;
    obj["pingAgeMs"] = ping.valid ? static_cast<int32_t>(now - ping.polledAtMs) : -1;
  }

  String payload;
  payload.reserve(256 + doc["servers"].size() * 260);
  serializeJson(doc, payload);

  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json", payload);
}

void WebDashboard::handleApiPoll() {
  monitor_.requestPoll();
  monitor_.requestPing();
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(202, "application/json", F("{\"queued\":true}"));
}

void WebDashboard::handleNotFound() {
  server_.send(404, "text/html; charset=utf-8",
               renderPage(F("Not found"), F("dashboard"),
                          F("<div class=\"msg\">Page not found. "
                            "<a href=\"/\">Back to the dashboard</a>.</div>")));
}
