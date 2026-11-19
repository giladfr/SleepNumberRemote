#pragma once
// Apple HomeKit bridge (HomeSpan): the remote joins Wi-Fi and shows up in the
// Home app with no hub, cloud or account. Home app commands go into the same
// bed command queue as the touchscreen.
//
// Published accessories (built at boot from settings):
//   <name> Side (one per side) <name> Flat, <name> Zero G, <name> Snore, <name> Favorite
//                              [, TV, Read] (switches: on while the bed is in that preset,
//                              off returns to flat)
//   Both Sides                 Flat, Zero G, Snore, Favorite[, TV, Read] for both sides
//   Bed Light                  only when the bed has an under-bed light
//
// HomeSpan can't be stopped or reconfigured while running, so changing these
// settings restarts the remote.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum class HkState : uint8_t { Off, Starting, Connecting, NeedsPairing, Paired };

struct HkStatus {
  HkState state = HkState::Off;
  char ssid[33] = "";
  char ip[16] = "";
  char code[12] = "";     // "123-45-678"
  char payload[24] = "";  // X-HM:// setup URI shown as a QR code
  uint32_t freeHeap = 0;  // bytes, internal RAM
  uint32_t minHeap = 0;   // lowest seen since boot
};

struct WifiNet {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

namespace HomeKit {

// Bit flags for RemoteSettings.bedFeatures: presets in the UI's order
// (flat, zero_g, snore, favorite, watch_tv, read), then the light.
constexpr uint8_t kFeatLight = 1 << 6;
constexpr uint8_t kFeatKnown = 1 << 7;
constexpr uint8_t kFeatDefault = 0x0F;  // flat, zero G, snore, favorite

void begin();  // after BedLink::begin(); starts HomeSpan in its own task if enabled
void loop();   // from the main loop: caches bed features, schedules restarts
HkStatus status();

// Each of these saves the change and asks for a restart (see pendingRestart).
void setEnabled(bool on);
void setWifi(const char* ssid, const char* password);
void resetPairing();  // forget every Apple Home pairing, new setup code
// Names changed: the Home app gets them after a restart while the screen is off.
void noteConfigChanged();

enum class Restart : uint8_t { None, Now, WhenIdle };
Restart pendingRestart();

void startWifiScan();
int wifiScan(WifiNet* out, int max);  // -1 while scanning, -2 failed, else count (strongest first)

// HomeKit setup URI: X-HM:// + 9 base-36 digits + the 4-character setup ID.
inline void setupPayload(const char* code, const char* setupId, uint8_t category, char* out, size_t n) {
  uint64_t v = ((uint64_t)category << 31) | ((uint64_t)2 << 27) /* IP */ | (uint64_t)(strtoul(code, nullptr, 10) & 0x7FFFFFF);
  char digits[10];
  for (int i = 8; i >= 0; i--, v /= 36) digits[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[v % 36];
  digits[9] = 0;
  snprintf(out, n, "X-HM://%s%.4s", digits, setupId);
}

constexpr uint8_t kCategoryBridge = 2;

}  // namespace HomeKit
