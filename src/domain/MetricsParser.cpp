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

void parseMemory(const String& value, ServerMetrics& metrics) {
  String text = value;
  text.replace("MB", "");
  text.replace("mb", "");

  const int slash = text.indexOf('/');
  if (slash <= 0) return;

  float used = 0;
  float total = 0;
  if (toNumber(text.substring(0, slash), used)) metrics.memUsedMb = used;
  if (toNumber(text.substring(slash + 1), total)) metrics.memTotalMb = total;
}

void parseTemperature(const String& value, ServerMetrics& metrics) {
  String text = value;
  text.replace("C", "");
  text.replace("c", "");
  text.replace("\xC2\xB0", "");  // UTF-8 degree sign

  float temp = 0;
  if (toNumber(text, temp)) metrics.tempC = temp;
}

}  // namespace

bool MetricsParser::parse(const String& output, ServerMetrics& metrics) {
  metrics.load1 = NAN;
  metrics.memUsedMb = NAN;
  metrics.memTotalMb = NAN;
  metrics.tempC = NAN;
  metrics.error = "";

  String value;
  const bool hasLoad = extractToken(output, "LOAD:", value);
  if (hasLoad) parseLoad(value, metrics);

  if (extractToken(output, "RAM:", value)) parseMemory(value, metrics);
  if (extractToken(output, "TEMP:", value)) parseTemperature(value, metrics);

  if (!hasLoad) {
    metrics.error = "Unexpected command output: " + output;
    return false;
  }
  return true;
}
