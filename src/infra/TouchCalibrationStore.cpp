#include "infra/TouchCalibrationStore.h"

#include <Preferences.h>

bool TouchCalibrationStore::load(uint16_t cal[kValueCount]) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  const size_t expected = sizeof(uint16_t) * kValueCount;
  const size_t length = prefs.getBytesLength(kKey);
  bool ok = false;
  if (length == expected) {
    ok = prefs.getBytes(kKey, cal, expected) == expected;
  }

  prefs.end();
  return ok;
}

bool TouchCalibrationStore::save(const uint16_t cal[kValueCount]) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  const size_t expected = sizeof(uint16_t) * kValueCount;
  const bool ok = prefs.putBytes(kKey, cal, expected) == expected;

  prefs.end();
  return ok;
}
