// Fake HomeKit: the same API as src/homekit.cpp, with time-scripted behaviour.
#include "sim_homekit.h"

#include <Arduino.h>
#include <string.h>

#include "settings.h"

namespace {
bool running = false;
uint32_t startedAt = 0, scanAt = 0;
bool paired = false;
HomeKit::Restart restart = HomeKit::Restart::None;

const WifiNet kNets[] = {{"HomeNet", -48, true}, {"HomeNet-Guest", -55, true}, {"Neighbors 5G", -71, true},
                         {"Cafe Free WiFi", -80, false}};

void startIfReady() {
  RemoteSettings& cfg = Settings::get();
  if (running || !cfg.homekit || !cfg.wifiSsid[0]) return;
  if (!cfg.hkCode[0]) {  // fixed values so screenshots are stable
    strlcpy(cfg.hkCode, "52816394", sizeof(cfg.hkCode));
    strlcpy(cfg.hkId, "7K2Q", sizeof(cfg.hkId));
  }
  running = true;
  startedAt = millis();
}
}  // namespace

namespace HomeKit {
void begin() { startIfReady(); }
void loop() {}

HkStatus status() {
  HkStatus st;
  const RemoteSettings& cfg = Settings::get();
  uint32_t age = millis() - startedAt;
  if (running) st.state = paired ? HkState::Paired : age < 800 ? HkState::Starting : age < 2500 ? HkState::Connecting : HkState::NeedsPairing;
  strlcpy(st.ssid, cfg.wifiSsid, sizeof(st.ssid));
  if (st.state == HkState::NeedsPairing || st.state == HkState::Paired) strlcpy(st.ip, "192.168.1.42", sizeof(st.ip));
  if (cfg.hkCode[0]) {
    snprintf(st.code, sizeof(st.code), "%.3s-%.2s-%.3s", cfg.hkCode, cfg.hkCode + 3, cfg.hkCode + 5);
    setupPayload(cfg.hkCode, cfg.hkId, kCategoryBridge, st.payload, sizeof(st.payload));
  }
  st.freeHeap = 96 * 1024;
  st.minHeap = 71 * 1024;
  return st;
}

void setEnabled(bool on) {
  Settings::get().homekit = on;
  if (running && !on) restart = Restart::Now;
  else startIfReady();
}

void setWifi(const char* ssid, const char* password) {
  strlcpy(Settings::get().wifiSsid, ssid, sizeof(Settings::get().wifiSsid));
  strlcpy(Settings::get().wifiPass, password, sizeof(Settings::get().wifiPass));
  if (running) restart = Restart::Now;
  else startIfReady();
}

void resetPairing() {
  strlcpy(Settings::get().hkCode, "30917485", sizeof(Settings::get().hkCode));
  paired = false;
  if (running) restart = Restart::Now;
}

void noteConfigChanged() {
  if (running && restart == Restart::None) restart = Restart::WhenIdle;
}

Restart pendingRestart() { return restart; }

void startWifiScan() { scanAt = millis(); }

int wifiScan(WifiNet* out, int max) {
  if (millis() - scanAt < 1500) return -1;
  int n = 0;
  for (const WifiNet& w : kNets)
    if (n < max) out[n++] = w;
  return n;
}
}  // namespace HomeKit

namespace SimHomeKit {
void pairPhone() { paired = true; }
void finishRestart() {
  restart = HomeKit::Restart::None;
  running = false;
  startIfReady();
}
}  // namespace SimHomeKit

// Local API stand-in: the simulator has no network.
#include "net_api.h"
namespace NetApi {
static bool on = true;
void setEnabled(bool v) { on = v; }
bool enabled() { return on; }
}  // namespace NetApi
