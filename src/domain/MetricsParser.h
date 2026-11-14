#pragma once

#include <Arduino.h>

#include "domain/ServerMetrics.h"

// Parses the single-line output of the remote stats command, e.g.
//   LOAD:0.08 RAM:143/7951MB TEMP:41.2C
// Missing fields are tolerated and reported as NAN.
namespace MetricsParser {

bool parse(const String& output, ServerMetrics& metrics);

}  // namespace MetricsParser
