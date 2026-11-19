#pragma once
// User settings persisted in flash (NVS). Defaults come from config.h.

#include <stdint.h>

struct RemoteSettings {
  bool swapSides;
  bool flipScreen;        // portrait upside down (USB on top)
  uint8_t brightness;     // 20-255
  uint16_t screenOffSec;  // 0 = never
  char bedAddr[18];       // "" = no bed selected
  uint8_t bedAddrType;    // NimBLE address type
  char sideName[2][12];   // on-screen names, [0] = left

  // HomeKit (see homekit.h). Wi-Fi details and the setup code stay on the device.
  bool homekit;
  char wifiSsid[33];
  char wifiPass[64];
  char hkCode[9];       // 8-digit setup code, generated on the device
  char hkId[5];         // setup ID for the QR code
  bool hkCodeApplied;   // HomeSpan has the verifier for hkCode
  uint8_t bedFeatures;  // HomeKit services to publish, cached from the bed (see homekit.cpp)
  bool api;             // local HTTP API while Wi-Fi is on (see net_api.h)
};

namespace Settings {
void load();
RemoteSettings& get();
void save();
}  // namespace Settings
