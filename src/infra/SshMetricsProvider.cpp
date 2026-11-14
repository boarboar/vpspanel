#include "infra/SshMetricsProvider.h"

#include <libssh/libssh.h>

#include "domain/MetricsParser.h"

namespace {

// Single round trip: load average, memory usage and CPU temperature.
const char kStatsCommand[] =
    R"CMD(echo "LOAD:$(cut -d ' ' -f1 /proc/loadavg 2>/dev/null) RAM:$(free -m 2>/dev/null | awk '/Mem:/ {print $3"/"$2"MB"}') TEMP:$(awk '{printf "%.1fC", $1/1000}' /sys/class/thermal/thermal_zone0/temp 2>/dev/null)")CMD";

constexpr size_t kMaxOutputBytes = 1024;
constexpr int kMaxReads = 64;

void closeSession(ssh_session session) {
  if (session == nullptr) return;
  ssh_disconnect(session);
  ssh_free(session);
}

}  // namespace

ServerMetrics SshMetricsProvider::fetch(const ServerConfig& server, uint16_t timeoutSec) {
  ServerMetrics metrics;

  const long timeout = timeoutSec > 0 ? timeoutSec : 10;
  const int readTimeoutMs = static_cast<int>(timeout * 1000);

  ssh_session session = ssh_new();
  if (session == nullptr) {
    metrics.error = "ssh_new() failed (out of memory?)";
    return metrics;
  }

  unsigned int port = server.port;
  int verbosity = SSH_LOG_NOLOG;

  ssh_options_set(session, SSH_OPTIONS_HOST, server.host.c_str());
  ssh_options_set(session, SSH_OPTIONS_PORT, &port);
  ssh_options_set(session, SSH_OPTIONS_USER, server.user.c_str());
  ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
  ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

  if (ssh_connect(session) != SSH_OK) {
    metrics.error = String("Connect failed: ") + ssh_get_error(session);
    closeSession(session);
    return metrics;
  }

  if (ssh_userauth_password(session, nullptr, server.password.c_str()) != SSH_AUTH_SUCCESS) {
    metrics.error = String("Authentication failed: ") + ssh_get_error(session);
    closeSession(session);
    return metrics;
  }

  ssh_channel channel = ssh_channel_new(session);
  if (channel == nullptr || ssh_channel_open_session(channel) != SSH_OK) {
    metrics.error = "Could not open SSH channel";
    if (channel != nullptr) ssh_channel_free(channel);
    closeSession(session);
    return metrics;
  }

  if (ssh_channel_request_exec(channel, kStatsCommand) != SSH_OK) {
    metrics.error = String("Command failed: ") + ssh_get_error(session);
  } else {
    String output;
    output.reserve(128);

    char buffer[192];
    int reads = 0;
    while (reads < kMaxReads && output.length() < kMaxOutputBytes) {
      const int n = ssh_channel_read_timeout(channel, buffer, sizeof(buffer) - 1, 0, readTimeoutMs);
      ++reads;
      if (n > 0) {
        output.concat(buffer, static_cast<unsigned int>(n));
      } else if (n != SSH_AGAIN) {
        break;
      }
    }

    output.trim();
    if (!MetricsParser::parse(output, metrics)) {
      // MetricsParser already filled in a description of the problem.
    } else {
      metrics.ok = true;
    }
  }

  ssh_channel_send_eof(channel);
  ssh_channel_close(channel);
  ssh_channel_free(channel);
  closeSession(session);

  return metrics;
}
