#include "infra/NvsWifiCredentialStore.h"

#include <Preferences.h>

bool NvsWifiCredentialStore::load(String& ssid, String& password) const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  const String storedSsid = prefs.getString(kSsidKey, "");
  const String storedPassword = prefs.getString(kPasswordKey, "");
  prefs.end();

  if (storedSsid.isEmpty()) return false;

  ssid = storedSsid;
  password = storedPassword;
  return true;
}

bool NvsWifiCredentialStore::save(const String& ssid, const String& password) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  const bool ssidWritten = prefs.putString(kSsidKey, ssid) == ssid.length();
  const bool passwordWritten =
      prefs.putString(kPasswordKey, password) == password.length();
  prefs.end();

  return ssidWritten && passwordWritten;
}
