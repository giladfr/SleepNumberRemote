// HomeKit bridge on HomeSpan. Runs in its own task: begin() blocks for a while
// and pairing does seconds of big-number math, neither of which may stall the UI.
// Services read a bed snapshot refreshed by that task and send commands through
// BedLink's queue, the same path as the touchscreen.

#include "homekit.h"

#include <Arduino.h>
#include <HomeSpan.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>

#include "bed_link.h"
#include "config.h"
#include "net_api.h"
#include "settings.h"

namespace {

struct PresetDef {
  const char* wire;
  const char* label;
};
// Same order as the bedFeatures bits.
const PresetDef kPresets[] = {{"flat", "Flat"},         {"zero_g", "Zero G"}, {"snore", "Snore"},
                              {"favorite", "Favorite"}, {"watch_tv", "TV"},   {"read", "Read"}};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

constexpr uint32_t kPresetGraceMs = 20000;  // a switch keeps its new state while the bed gets there

volatile bool running = false;
volatile HkState hkState = HkState::Off;
volatile HomeKit::Restart restartReq = HomeKit::Restart::None;
volatile bool codeApplied = false;  // set by the task, saved by loop()
bool scanning = false;
uint32_t scanStartedAt = 0;
constexpr uint32_t kScanTimeoutMs = 20000;

// Copied from Settings before the task starts, so the task never reads settings
// the UI may be changing.
struct {
  char ssid[33], pass[64], code[9], id[5], names[2][12];
  bool applyCode;
  uint8_t features;
} boot;

BedState bed;  // owned by the HomeKit task

void send(CmdType type, BedSide side, int value, Actuator act = ACT_HEAD, const char* preset = nullptr) {
  BedCmd c;
  c.type = type;
  c.side = side;
  c.value = value;
  c.actuator = act;
  if (preset) strlcpy(c.preset, preset, sizeof(c.preset));
  BedLink::send(c);
}

// ---- Services ------------------------------------------------------------------

// A preset as a switch for one side or both (kBoth): on while the bed (both sides)
// is in that preset. Turning it off goes back to flat.
constexpr int kBoth = 2;

struct PresetSwitch : Service::Switch {
  int side;
  int idx;
  SpanCharacteristic* on;
  uint32_t changedAt = 0;
  bool changed = false;

  PresetSwitch(int s, int i, const char* name) : Service::Switch(), side(s), idx(i) {
    new Characteristic::Name(name);
    new Characteristic::ConfiguredName(name);  // what current iOS shows (it ignores Name here)
    on = new Characteristic::On(false);
  }

  void apply(const char* preset) {
    for (int s = 0; s < 2; s++)
      if (side == kBoth || side == s) send(CmdType::Preset, (BedSide)s, 0, ACT_HEAD, preset);
  }

  // Called for any write to this service, including the Home app saving the
  // ConfiguredName during setup, so act only on a real On change, and only when
  // it changes something: on = go to the preset unless already there; off = go
  // flat only if the bed is in this preset (never for Flat itself). Presets are
  // empty (unknown) while the bed link sleeps; then on always sends and off only
  // sends if the switch was showing on.
  boolean update() override {
    if (!on->updated()) return true;
    bool want = on->getNewVal();
    bool known = bed.preset[0][0] && bed.preset[1][0];
    bool is = known && isNow();
    Serial.printf("[homekit] %s %s\n", kPresets[idx].label, want ? "on" : "off");
    if (want && !is) apply(kPresets[idx].wire);
    else if (!want && idx != 0 && (known ? anySideIs() : on->getVal())) apply("flat");
    changed = true;
    changedAt = millis();
    return true;
  }

  bool sideIs(int s) const { return strcmp(bed.preset[s], kPresets[idx].wire) == 0; }
  bool isNow() const { return side == kBoth ? sideIs(0) && sideIs(1) : sideIs(side); }
  bool anySideIs() const { return side == kBoth ? sideIs(0) || sideIs(1) : sideIs(side); }

  void loop() override {
    if (changed && millis() - changedAt < kPresetGraceMs) return;
    changed = false;
    if (!bed.preset[0][0] || !bed.preset[1][0]) return;  // not read from the bed yet
    bool is = isNow();
    if (on->getVal() != is) on->setVal(is);
  }
};

struct BedLight : Service::LightBulb {
  SpanCharacteristic* on;
  uint32_t changedAt = 0;
  bool changed = false;

  BedLight() : Service::LightBulb() {
    new Characteristic::Name("Bed Light");
    new Characteristic::ConfiguredName("Bed Light");
    on = new Characteristic::On(false);
  }

  boolean update() override {
    if (!on->updated()) return true;  // e.g. a name write from the Home app
    send(CmdType::Light, SIDE_LEFT, on->getNewVal() ? 1 : 0);
    changed = true;
    changedAt = millis();
    return true;
  }

  void loop() override {
    if (changed && millis() - changedAt < 10000) return;
    changed = false;
    if (bed.configLoaded && bed.connectedAt && on->getVal() != bed.lightOn) on->setVal(bed.lightOn);
  }
};

void accessoryInfo(const char* name) {
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name(name);
  new Characteristic::Manufacturer("Bed Remote (DIY)");
  new Characteristic::Model("Sleep Number remote");
  new Characteristic::FirmwareRevision(FW_VERSION);
}

void buildAccessories() {
  new SpanAccessory();  // the bridge
  accessoryInfo("Bed Remote");

  char name[40];
  for (int s = 0; s < 2; s++) {
    const char* n = boot.names[s];
    new SpanAccessory();
    snprintf(name, sizeof(name), "%s Side", n);
    accessoryInfo(name);
    for (int i = 0; i < kPresetCount; i++) {
      if (!(boot.features & (1 << i))) continue;
      snprintf(name, sizeof(name), "%s %s", n, kPresets[i].label);
      new PresetSwitch(s, i, name);
    }
  }

  if (boot.features & HomeKit::kFeatLight) {
    new SpanAccessory();
    accessoryInfo("Bed Light");
    new BedLight();
  }

  // Both sides at once, named after the preset alone ("Hey Siri, turn on Zero G").
  new SpanAccessory();
  accessoryInfo("Both Sides");
  for (int i = 0; i < kPresetCount; i++)
    if (boot.features & (1 << i)) new PresetSwitch(kBoth, i, kPresets[i].label);
}

void onStatus(HS_STATUS s) {
  switch (s) {
    case HS_PAIRING_NEEDED: hkState = HkState::NeedsPairing; break;
    case HS_PAIRED: hkState = HkState::Paired; break;
    case HS_WIFI_NEEDED:
    case HS_WIFI_CONNECTING: hkState = HkState::Connecting; break;
    default: break;
  }
  Serial.printf("[homekit] status %d, free heap %u\n", (int)s, (unsigned)ESP.getFreeHeap());
}

void homekitTask(void*) {
  homeSpan.setLogLevel(0);
  homeSpan.setSerialInputDisable(true);  // everything is configured on the touchscreen
  homeSpan.setStatusCallback(onStatus);
  homeSpan.setQRID(boot.id);
  homeSpan.setHostNameSuffix("");  // bedremote.local, for the local API too
  homeSpan.setWifiCallback([] {
    // With an IPv6 link-local address mDNS answers AAAA queries too; without one,
    // macOS waits 5 s for that answer before every bedremote.local lookup.
    WiFi.enableIpV6();
    NetApi::wifiUp(boot.names);
  });
  homeSpan.setWifiCredentials(boot.ssid, boot.pass);
  if (boot.applyCode) {
    homeSpan.setPairingCode(boot.code);  // stores the SRP verifier, not the code
    codeApplied = true;
  }
  homeSpan.begin(Category::Bridges, "Bed Remote", NetApi::kHostName, "Sleep Number remote");
  buildAccessories();

  uint32_t lastSnap = 0, lastMem = 0;
  for (;;) {
    if (millis() - lastMem > 60000) {
      lastMem = millis();
      Serial.printf("[homekit] heap %u (min %u, largest %u), stack left %u\n", (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
    if (millis() - lastSnap > 400) {
      bed = BedLink::snapshot();
      lastSnap = millis();
    }
    homeSpan.poll();
    NetApi::poll();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void newSetupCode(RemoteSettings& cfg) {
  static const char* const kRejected[] = {"00000000", "11111111", "22222222", "33333333", "44444444", "55555555",
                                          "66666666", "77777777", "88888888", "99999999", "12345678", "87654321"};
  for (;;) {
    snprintf(cfg.hkCode, sizeof(cfg.hkCode), "%08lu", (unsigned long)(esp_random() % 100000000UL));
    bool ok = true;
    for (const char* r : kRejected) ok &= strcmp(cfg.hkCode, r) != 0;
    if (ok) break;
  }
  for (int i = 0; i < 4; i++) cfg.hkId[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[esp_random() % 36];
  cfg.hkId[4] = 0;
  cfg.hkCodeApplied = false;
}

// Start HomeSpan if it is enabled and has a network; it can't be restarted
// without rebooting, so this only ever runs once.
void startIfReady() {
  RemoteSettings& cfg = Settings::get();
  if (running || !cfg.homekit || !cfg.wifiSsid[0]) return;
  if (!cfg.hkCode[0]) {
    newSetupCode(cfg);
    Settings::save();
  }
  strlcpy(boot.ssid, cfg.wifiSsid, sizeof(boot.ssid));
  strlcpy(boot.pass, cfg.wifiPass, sizeof(boot.pass));
  strlcpy(boot.code, cfg.hkCode, sizeof(boot.code));
  strlcpy(boot.id, cfg.hkId, sizeof(boot.id));
  for (int s = 0; s < 2; s++) {
    const char* n = cfg.sideName[s];
    strlcpy(boot.names[s], n[0] ? n : (s == SIDE_LEFT ? LEFT_NAME : RIGHT_NAME), sizeof(boot.names[s]));
  }
  boot.applyCode = !cfg.hkCodeApplied;
  NetApi::setEnabled(cfg.api);
  boot.features = cfg.bedFeatures & HomeKit::kFeatKnown ? cfg.bedFeatures : HomeKit::kFeatDefault;
  running = true;
  hkState = HkState::Starting;
  if (scanning) WiFi.scanDelete();
  scanning = false;
  // Core 0 next to Wi-Fi and Bluetooth, below the bed link task's priority.
  xTaskCreatePinnedToCore(homekitTask, "homekit", 9216, nullptr, 1, nullptr, 0);
}

void requestRestart(HomeKit::Restart r) {
  if (r == HomeKit::Restart::Now || restartReq == HomeKit::Restart::None) restartReq = r;
}

uint8_t featuresOf(const BedState& b) {
  const bool presets[kPresetCount] = {b.presetFlat, b.presetZeroG, b.presetSnore, b.presetFavorite, b.presetTv, b.presetRead};
  uint8_t f = HomeKit::kFeatKnown | (b.hasLight ? HomeKit::kFeatLight : 0);
  for (int i = 0; i < kPresetCount; i++) f |= presets[i] ? 1 << i : 0;
  return f;
}

}  // namespace

namespace HomeKit {

void begin() { startIfReady(); }

void loop() {
  RemoteSettings& cfg = Settings::get();
  if (codeApplied && !cfg.hkCodeApplied) {
    cfg.hkCodeApplied = true;
    Settings::save();
  }
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 2000) return;
  lastCheck = millis();
  // Remember which presets and light the bed has, so the next boot publishes
  // exactly those (the bed is only read after HomeKit has started).
  BedState b = BedLink::snapshot();
  if (b.configLoaded && featuresOf(b) != cfg.bedFeatures) {
    uint8_t published = boot.features;
    cfg.bedFeatures = featuresOf(b);
    Settings::save();
    if (running && (cfg.bedFeatures & ~kFeatKnown) != (published & ~kFeatKnown)) requestRestart(Restart::WhenIdle);
  }
}

HkStatus status() {
  HkStatus st;
  const RemoteSettings& cfg = Settings::get();
  st.state = running ? hkState : HkState::Off;
  strlcpy(st.ssid, cfg.wifiSsid, sizeof(st.ssid));
  if (running && WiFi.isConnected()) strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof(st.ip));
  if (cfg.hkCode[0]) {
    snprintf(st.code, sizeof(st.code), "%.3s-%.2s-%.3s", cfg.hkCode, cfg.hkCode + 3, cfg.hkCode + 5);
    setupPayload(cfg.hkCode, cfg.hkId, kCategoryBridge, st.payload, sizeof(st.payload));
  }
  st.freeHeap = ESP.getFreeHeap();
  st.minHeap = ESP.getMinFreeHeap();
  return st;
}

void setEnabled(bool on) {
  RemoteSettings& cfg = Settings::get();
  cfg.homekit = on;
  Settings::save();
  if (running && !on) requestRestart(Restart::Now);  // the only way to stop HomeSpan
  else startIfReady();
}

void setWifi(const char* ssid, const char* password) {
  RemoteSettings& cfg = Settings::get();
  strlcpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid));
  strlcpy(cfg.wifiPass, password, sizeof(cfg.wifiPass));
  Settings::save();
  if (running) requestRestart(Restart::Now);
  else startIfReady();
}

void resetPairing() {
  newSetupCode(Settings::get());
  Settings::save();
  // HomeSpan's pairing records and device ID: erased, so the Home app sees a new accessory.
  Preferences hap;
  if (hap.begin("HAP", false)) {
    hap.clear();
    hap.end();
  }
  if (running) requestRestart(Restart::Now);
}

void noteConfigChanged() {
  if (running) requestRestart(Restart::WhenIdle);
}

Restart pendingRestart() { return restartReq; }

void startWifiScan() {
  if (scanning) return;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  scanStartedAt = millis();
  int r = WiFi.scanNetworks(true);
  scanning = r == WIFI_SCAN_RUNNING;
  Serial.printf("[wifi] scan start: %d\n", r);
}

int wifiScan(WifiNet* out, int max) {
  int n = WiFi.scanComplete();
  uint32_t age = millis() - scanStartedAt;
  // Arduino reports WIFI_SCAN_FAILED after a fixed 6 s, but sharing the radio with
  // the bed's Bluetooth link makes a full scan take longer. Keep waiting for the
  // real result while the scan is still going.
  if (n == WIFI_SCAN_RUNNING || (n == WIFI_SCAN_FAILED && scanning && age < kScanTimeoutMs)) return -1;
  Serial.printf("[wifi] scan done: %d networks in %u ms, free heap %u\n", n, (unsigned)age, (unsigned)ESP.getFreeHeap());
  if (n < 0) {  // Wi-Fi didn't start or the scan failed
    scanning = false;
    if (!running) WiFi.mode(WIFI_OFF);
    return -2;
  }
  int count = 0;
  for (int i = 0; i < n && count < max; i++) {  // results come strongest first
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden network
    bool dup = false;
    for (int j = 0; j < count; j++) dup |= ssid == out[j].ssid;
    if (dup) continue;
    WifiNet& w = out[count++];
    strlcpy(w.ssid, ssid.c_str(), sizeof(w.ssid));  // raw: it is used to join (the UI cleans it for display)
    w.rssi = WiFi.RSSI(i);
    w.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  scanning = false;
  if (!running) WiFi.mode(WIFI_OFF);  // only on for the scan until HomeKit starts
  return count;
}

}  // namespace HomeKit
