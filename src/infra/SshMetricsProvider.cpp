#include "infra/SshMetricsProvider.h"

#include <libssh/libssh.h>

#include "domain/MetricsParser.h"

namespace {

// Single round trip: CPU usage, load average, memory and root filesystem use.
const char kStatsCommand[] =
    R"CMD(echo "LOAD:$(cut -d ' ' -f1 /proc/loadavg 2>/dev/null) RAM:$(free -m 2>/dev/null | awk '/Mem:/ {print $3"/"$2"MB"}') DISK:$(df -Pk / 2>/dev/null | awk 'NR==2 {print int($3/1024)"/"int($2/1024)}') CPU:$(a=$(awk '/^cpu /{t=0;for(i=2;i<=9;i++)t+=$i;print t, $5+$6}' /proc/stat 2>/dev/null); sleep 0.3; b=$(awk '/^cpu /{t=0;for(i=2;i<=9;i++)t+=$i;print t, $5+$6}' /proc/stat 2>/dev/null); set -- $a $b; LC_ALL=C awk -v t1=$1 -v i1=$2 -v t2=$3 -v i2=$4 'BEGIN{dt=t2-t1;di=i2-i1;if(dt<=0){printf "0.0";exit}d=dt-di;if(d<0)d=0;printf "%.1f",100*d/dt}')")CMD";

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
