#pragma once

#include <Arduino.h>

#include "domain/ServerMetrics.h"

// Parses the single-line output of the remote stats command, e.g.
//   LOAD:0.08 RAM:143/7951MB DISK:4123/40258 CORES:8 UPTIME:12345.67 CPU:12.5
// Missing fields are tolerated and reported as NAN.
namespace MetricsParser {

bool parse(const String& output, ServerMetrics& metrics);

}  // namespace MetricsParser
