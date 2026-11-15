#pragma once

#include <Arduino.h>

// Persists the XPT2046 touch calibration (the five values produced by
// TFT_eSPI::calibrateTouch) in the NVS flash partition, so the calibration
// survives reboots and firmware updates.
class TouchCalibrationStore {
 public:
  static constexpr size_t kValueCount = 5;

  // Fills `cal` and returns true when a stored calibration is available.
  bool load(uint16_t cal[kValueCount]);

  bool save(const uint16_t cal[kValueCount]);

 private:
  static constexpr const char* kNamespace = "vpspanel";
  static constexpr const char* kKey = "touchcal";
};
