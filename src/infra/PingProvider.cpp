#include "infra/PingProvider.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <ping/ping_sock.h>

#include <new>

namespace {

// One echo request with a short reply timeout. A single probe is enough for a
// reachability indicator and keeps a cycle with many unreachable hosts short.
constexpr uint32_t kProbeTimeoutMs = 1000;
constexpr uint32_t kProbeDataSize = 32;
// Extra time the session may need to finish after the per-packet timeout so the
// internal ping task can report its result before the session is deleted.
constexpr uint32_t kFinishSlackMs = 1500;

// Heap allocated because the lwIP ping callbacks run on its own task; if a
// session ever fails to report its end, the object is intentionally leaked
// rather than freed while a late callback could still touch it.
struct PingContext {
  SemaphoreHandle_t done = nullptr;
  bool replied = false;
  uint32_t startedAtMs = 0;
  uint32_t rttMs = 0;
};

void onPingSuccess(esp_ping_handle_t handle, void* args) {
  auto* context = static_cast<PingContext*>(args);
  uint32_t elapsed = 0;
  if (esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed)) == ESP_OK) {
    context->rttMs = elapsed;
  } else {
    context->rttMs = millis() - context->startedAtMs;
  }
  context->replied = true;
}

void onPingTimeout(esp_ping_handle_t, void*) {}

void onPingEnd(esp_ping_handle_t, void* args) {
  auto* context = static_cast<PingContext*>(args);
  if (context->done != nullptr) xSemaphoreGive(context->done);
}

// lwIP needs a numeric address, so host names are resolved first. The lookup
// runs on the polling task and is therefore invisible to the web UI.
bool resolveIpv4(const String& host, ip4_addr_t& address) {
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* info = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &info) != 0 || info == nullptr) {
    return false;
  }

  const auto* resolved = reinterpret_cast<const struct sockaddr_in*>(info->ai_addr);
  address.addr = resolved->sin_addr.s_addr;
  freeaddrinfo(info);
  return true;
}

}  // namespace

PingResult PingProvider::ping(const ServerConfig& server) {
  PingResult result;

  ip4_addr_t address = {};
  if (!resolveIpv4(server.host, address)) {
    result.error = "Host name lookup failed";
    return result;
  }

  auto* context = new (std::nothrow) PingContext();
  if (context == nullptr) {
    result.error = "Out of memory";
    return result;
  }
  context->done = xSemaphoreCreateBinary();
  if (context->done == nullptr) {
    delete context;
    result.error = "Out of memory";
    return result;
  }

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 1;
  // A single probe needs no inter-packet interval. Keeping it at zero also
  // avoids the lwIP ping task's trailing delay before it reports the end of
  // the session, which would otherwise add 1 s per reachable host.
  config.interval_ms = 0;
  config.timeout_ms = kProbeTimeoutMs;
  config.data_size = kProbeDataSize;
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4 = address;

  esp_ping_callbacks_t callbacks = {};
  callbacks.cb_args = context;
  callbacks.on_ping_success = onPingSuccess;
  callbacks.on_ping_timeout = onPingTimeout;
  callbacks.on_ping_end = onPingEnd;

  esp_ping_handle_t handle = nullptr;
  const esp_err_t created = esp_ping_new_session(&config, &callbacks, &handle);
  if (created != ESP_OK) {
    vSemaphoreDelete(context->done);
    delete context;
    result.error = String("Ping session failed: ") + esp_err_to_name(created);
    return result;
  }

  context->startedAtMs = millis();
  const esp_err_t started = esp_ping_start(handle);
  if (started != ESP_OK) {
    esp_ping_delete_session(handle);
    vSemaphoreDelete(context->done);
    delete context;
    result.error = String("Ping start failed: ") + esp_err_to_name(started);
    return result;
  }

  bool finished = xSemaphoreTake(context->done,
                                 pdMS_TO_TICKS(kProbeTimeoutMs + kFinishSlackMs)) == pdTRUE;
  if (!finished) {
    // Defensive: ask the session to stop and give it a moment to invoke the end
    // callback. If it still does not, the context is leaked on purpose so a
    // late callback cannot use freed memory.
    esp_ping_stop(handle);
    finished = xSemaphoreTake(context->done, pdMS_TO_TICKS(500)) == pdTRUE;
  }

  esp_ping_delete_session(handle);

  const bool reachable = context->replied;
  const uint32_t rttMs = context->rttMs;
  result.error = finished ? "No ICMP reply" : "Ping timed out";
  if (finished) {
    vSemaphoreDelete(context->done);
    delete context;
  }

  result.valid = true;
  result.reachable = reachable;
  if (reachable) {
    result.rttMs = static_cast<float>(rttMs);
    result.error = "";
  }
  return result;
}
