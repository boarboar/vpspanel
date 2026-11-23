#include "domain/MonitorService.h"

#include <set>

namespace {

uint32_t markMillis() {
  const uint32_t now = millis();
  return now == 0 ? 1 : now;  // 0 is used as "never"
}

}  // namespace

MonitorService::MonitorService(ServerRegistry& registry, IMetricsProvider& provider,
                               IPingProvider& pingProvider)
    : registry_(registry), provider_(provider), pingProvider_(pingProvider) {}

void MonitorService::begin() {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateMutex();
  }
}

void MonitorService::setListener(Listener* listener) {
  listener_ = listener;
}

bool MonitorService::pollDue() {
  if (immediateRequest_.exchange(false)) return true;

  const uint32_t now = millis();
  if (lastCycleAtMs_ == 0) return true;

  const uint32_t intervalMs = static_cast<uint32_t>(registry_.settings().sshIntervalSec) * 1000UL;
  return (now - lastCycleAtMs_) >= intervalMs;
}

void MonitorService::requestPoll() {
  immediateRequest_.store(true);
}

bool MonitorService::isPolling() const {
  return polling_.load();
}

uint32_t MonitorService::lastCycleAtMs() const {
  return lastCycleAtMs_;
}

bool MonitorService::pingDue() {
  if (immediatePingRequest_.exchange(false)) return true;

  const uint32_t now = millis();
  if (lastPingCycleAtMs_ == 0) return true;

  const uint32_t intervalMs = static_cast<uint32_t>(registry_.settings().pingIntervalSec) * 1000UL;
  return (now - lastPingCycleAtMs_) >= intervalMs;
}

void MonitorService::requestPing() {
  immediatePingRequest_.store(true);
}

bool MonitorService::isPinging() const {
  return pinging_.load();
}

uint32_t MonitorService::lastPingCycleAtMs() const {
  return lastPingCycleAtMs_;
}

void MonitorService::pollAll() {
  polling_.store(true);

  const std::vector<ServerConfig> servers = registry_.servers();
  const uint16_t timeoutSec = registry_.settings().sshTimeoutSec;
  const uint32_t cycleStart = millis();

  if (listener_) listener_->onCycleStarted(servers.size());

  size_t polled = 0;
  for (const auto& server : servers) {
    if (!server.enabled) continue;

    if (listener_) listener_->onPollStarted(server);

    const uint32_t started = millis();
    ServerMetrics metrics = provider_.fetch(server, timeoutSec);
    metrics.valid = true;
    metrics.polledAtMs = markMillis();
    metrics.durationMs = millis() - started;

    {
      Guard guard(mutex_);
      metrics_[server.id] = metrics;
    }

    if (listener_) listener_->onPollFinished(server, metrics);
    ++polled;

    // Let the network stack and the web server breathe between hosts.
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  // Drop measurements of servers that were removed from the configuration.
  {
    std::set<String> knownIds;
    for (const auto& server : servers) knownIds.insert(server.id);

    Guard guard(mutex_);
    for (auto it = metrics_.begin(); it != metrics_.end();) {
      if (knownIds.count(it->first) == 0) {
        it = metrics_.erase(it);
      } else {
        ++it;
      }
    }
  }

  lastCycleAtMs_ = markMillis();
  polling_.store(false);

  if (listener_) listener_->onCycleFinished(polled, millis() - cycleStart);
}

void MonitorService::pingAll() {
  pinging_.store(true);

  const std::vector<ServerConfig> servers = registry_.servers();
  const uint32_t cycleStart = millis();

  if (listener_) listener_->onPingCycleStarted(servers.size());

  size_t polled = 0;
  for (const auto& server : servers) {
    if (!server.enabled) continue;

    PingResult ping = pingProvider_.ping(server);
    ping.polledAtMs = markMillis();

    {
      Guard guard(mutex_);
      pings_[server.id] = ping;
    }

    if (listener_) listener_->onPingFinished(server, ping);
    ++polled;

    // The ICMP probes are cheap, but yielding keeps the web server responsive.
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  // Drop probes of servers that were removed from the configuration.
  {
    std::set<String> knownIds;
    for (const auto& server : servers) knownIds.insert(server.id);

    Guard guard(mutex_);
    for (auto it = pings_.begin(); it != pings_.end();) {
      if (knownIds.count(it->first) == 0) {
        it = pings_.erase(it);
      } else {
        ++it;
      }
    }
  }

  lastPingCycleAtMs_ = markMillis();
  pinging_.store(false);

  if (listener_) listener_->onPingCycleFinished(polled, millis() - cycleStart);
}

std::vector<MonitorService::Snapshot> MonitorService::snapshots() const {
  const std::vector<ServerConfig> servers = registry_.servers();
  std::vector<Snapshot> result;
  result.reserve(servers.size());

  Guard guard(mutex_);
  for (const auto& server : servers) {
    Snapshot snapshot;
    snapshot.server = server;

    auto metricsIt = metrics_.find(server.id);
    if (metricsIt != metrics_.end()) {
      snapshot.metrics = metricsIt->second;
    }

    auto pingIt = pings_.find(server.id);
    if (pingIt != pings_.end()) {
      snapshot.ping = pingIt->second;
    }

    result.push_back(snapshot);
  }
  return result;
}
