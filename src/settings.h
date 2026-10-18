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
};

namespace Settings {
void load();
RemoteSettings& get();
void save();
}  // namespace Settings
