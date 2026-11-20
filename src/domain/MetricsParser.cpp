#include "domain/MetricsParser.h"

#include <cstdlib>
#include <cstring>

namespace {

bool extractToken(const String& text, const char* key, String& value) {
  const int keyLen = static_cast<int>(strlen(key));
  const int idx = text.indexOf(key);
  if (idx < 0) return false;

  int end = idx + keyLen;
  while (end < static_cast<int>(text.length())) {
    const char c = text[end];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
    ++end;
  }

  value = text.substring(idx + keyLen, end);
  value.trim();
  return value.length() > 0;
}

bool toNumber(const String& text, float& out) {
  String value = text;
  value.trim();
  if (value.length() == 0) return false;

  const char first = value[0];
  if (!(isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+' || first == '.')) {
    return false;
  }
  out = value.toFloat();
  return true;
}

void parseLoad(const String& value, ServerMetrics& metrics) {
  float load = 0;
  if (toNumber(value, load)) metrics.load1 = load;
}

void parseCpu(const String& value, ServerMetrics& metrics) {
  String text = value;
  text.replace("%", "");

  float percent = 0;
  if (!toNumber(text, percent)) return;

  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  metrics.cpuPercent = percent;
}

// Parses "used/total" (an optional MB suffix is tolerated) into the given pair.
void parseUsage(const String& value, float& usedOut, float& totalOut) {
  String text = value;
  text.replace("MB", "");
  text.replace("mb", "");

  const int slash = text.indexOf('/');
  if (slash <= 0) return;

  float used = 0;
  float total = 0;
  if (toNumber(text.substring(0, slash), used)) usedOut = used;
  if (toNumber(text.substring(slash + 1), total)) totalOut = total;
}

}  // namespace

bool MetricsParser::parse(const String& output, ServerMetrics& metrics) {
  metrics.cpuPercent = NAN;
  metrics.load1 = NAN;
  metrics.cpuCores = NAN;
  metrics.memUsedMb = NAN;
  metrics.memTotalMb = NAN;
  metrics.diskUsedMb = NAN;
  metrics.diskTotalMb = NAN;
  metrics.uptimeSec = NAN;
  metrics.error = "";

  String value;
  const bool hasLoad = extractToken(output, "LOAD:", value);
  if (hasLoad) parseLoad(value, metrics);

  if (extractToken(output, "RAM:", value)) {
    parseUsage(value, metrics.memUsedMb, metrics.memTotalMb);
  }
  if (extractToken(output, "DISK:", value)) {
    parseUsage(value, metrics.diskUsedMb, metrics.diskTotalMb);
  }
  if (extractToken(output, "CORES:", value)) {
    float cores = 0;
    if (toNumber(value, cores) && cores > 0) metrics.cpuCores = cores;
  }
  if (extractToken(output, "UPTIME:", value)) {
    float uptime = 0;
    if (toNumber(value, uptime) && uptime >= 0) metrics.uptimeSec = uptime;
  }
  if (extractToken(output, "CPU:", value)) parseCpu(value, metrics);

  if (!hasLoad) {
    metrics.error = "Unexpected command output: " + output;
    return false;
  }
  return true;
}
