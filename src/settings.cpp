#include "settings.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace {
RemoteSettings settings;
Preferences prefs;

// getString() logs an error for keys never saved; check first so first boot is quiet.
void loadString(const char* key, const char* fallback, char* out, size_t n) {
  strlcpy(out, prefs.isKey(key) ? prefs.getString(key, fallback).c_str() : fallback, n);
}
}  // namespace

namespace Settings {

void load() {
  prefs.begin("remote", false);
  settings.swapSides = prefs.getBool("swap", SWAP_BED_SIDES);
  settings.flipScreen = prefs.getBool("flip", FLIP_SCREEN);
  settings.brightness = prefs.getUChar("bright", BACKLIGHT_ON);
  settings.screenOffSec = prefs.getUShort("off", OFF_AFTER_MS / 1000);
  loadString("addr", "", settings.bedAddr, sizeof(settings.bedAddr));
  settings.bedAddrType = prefs.getUChar("addrt", 0);
  loadString("name0", LEFT_NAME, settings.sideName[0], sizeof(settings.sideName[0]));
  loadString("name1", RIGHT_NAME, settings.sideName[1], sizeof(settings.sideName[1]));
  settings.homekit = prefs.getBool("hk", false);
  loadString("wssid", "", settings.wifiSsid, sizeof(settings.wifiSsid));
  loadString("wpass", "", settings.wifiPass, sizeof(settings.wifiPass));
  loadString("hkcode", "", settings.hkCode, sizeof(settings.hkCode));
  loadString("hkid", "", settings.hkId, sizeof(settings.hkId));
  settings.hkCodeApplied = prefs.getBool("hkset", false);
  settings.bedFeatures = prefs.getUChar("feat", 0);
  settings.api = prefs.getBool("api", true);
}

RemoteSettings& get() { return settings; }

void save() {
  prefs.putBool("swap", settings.swapSides);
  prefs.putBool("flip", settings.flipScreen);
  prefs.putUChar("bright", settings.brightness);
  prefs.putUShort("off", settings.screenOffSec);
  prefs.putString("addr", settings.bedAddr);
  prefs.putUChar("addrt", settings.bedAddrType);
  prefs.putString("name0", settings.sideName[0]);
  prefs.putString("name1", settings.sideName[1]);
  prefs.putBool("hk", settings.homekit);
  prefs.putString("wssid", settings.wifiSsid);
  prefs.putString("wpass", settings.wifiPass);
  prefs.putString("hkcode", settings.hkCode);
  prefs.putString("hkid", settings.hkId);
  prefs.putBool("hkset", settings.hkCodeApplied);
  prefs.putUChar("feat", settings.bedFeatures);
  prefs.putBool("api", settings.api);
}

}  // namespace Settings
