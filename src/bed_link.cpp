#include "bed_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "settings.h"

namespace {

const NimBLEUUID kService("09d23fae-90e6-44c2-95b6-0b3d0f1abf25");
const NimBLEUUID kBamkey("421e00f3-ae76-4c49-ab6e-39e4df4a5333");
const NimBLEUUID kAuth("8d4675a5-b5fa-42b2-b587-0ee71c46b709");

const char* const kActName[2] = {"head", "foot"};

constexpr uint32_t kHintTimeoutMs = 7000;
constexpr uint32_t kPollFastMs = 800;
constexpr uint32_t kPollSlowMs = 5000;
constexpr uint32_t kFastAfterCmdMs = 15000;
constexpr uint32_t kMotionGraceMs = 3000;
constexpr uint32_t kMaxFrame = 1024;  // real responses are < 100 bytes

SemaphoreHandle_t stateMutex;
SemaphoreHandle_t hintSem;
QueueHandle_t cmdQueue;
BedState state;
volatile bool active = true;

// Requests from the UI, handled by the link task between operations.
volatile bool reqScan = false, reqForget = false, reqReconnect = false, reqPair = false;
FoundBed pairTarget;

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* bamkeyChar = nullptr;
NimBLEAddress bedAddr;
bool haveAddr = false;
bool ready = false;
uint8_t session[16];
int authFailures = 0;
int cmdFailures = 0;
bool lastLinkOk = true;  // false when the last bamkey failed in transport (not a bed "FAIL" reply)

int pollIdx = 0;
uint32_t nextPollAt = 0;
uint32_t fastUntil = 0;
uint32_t lastMotionCmdAt = 0;
bool sawMotion = false;

template <typename F>
void mutate(F f) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  f(state);
  state.version++;
  xSemaphoreGive(stateMutex);
}

void setLink(LinkState s, const char* msg) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool changed = state.link != s || strcmp(state.status, msg) != 0;
  if (changed) {
    state.link = s;
    strlcpy(state.status, msg, sizeof(state.status));
    if (s == LinkState::Error || s == LinkState::Busy) strlcpy(state.lastError, msg, sizeof(state.lastError));
    state.version++;
  }
  xSemaphoreGive(stateMutex);
  if (changed) Serial.printf("[bed] %s\n", msg);
}

// On-screen side -> bed wire side.
const char* wire(int side) { return (side == SIDE_LEFT) != Settings::get().swapSides ? "left" : "right"; }
// Bed wire side -> on-screen side.
int screenSide(bool wireLeft) { return wireLeft != Settings::get().swapSides ? SIDE_LEFT : SIDE_RIGHT; }

bool controlPending() { return reqScan || reqForget || reqReconnect || reqPair; }

// Sleep, but wake early for UI requests.
void waitMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms && !controlPending()) vTaskDelay(pdMS_TO_TICKS(50));
}

// ---- Fuzion framing ---------------------------------------------------------

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return ~c;
}

void put32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; i++) s += char((v >> (8 * i)) & 0xFF);
}

uint32_t get32(const std::string& s, size_t o) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; i--) v = (v << 8) | uint8_t(s[o + i]);
  return v;
}

// Text from the radio (bed replies, advertised names) is shown on screen:
// keep printable ASCII only, and drop '#' (LVGL recolor markup in the log).
void sanitize(char* s) {
  for (; *s; s++)
    if (*s < 0x20 || *s > 0x7E || *s == '#') *s = '?';
}

void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  static const uint8_t zero[16] = {0};
  if (len == 16 && (memcmp(data, session, 16) == 0 || memcmp(data, zero, 16) == 0)) xSemaphoreGive(hintSem);
}

// One request/response; `resp` gets the response text (or an error description).
bool bamkeyRaw(const String& cmd, String& resp) {
  lastLinkOk = false;
  if (!client || !client->isConnected() || !bamkeyChar) {
    resp = "not connected";
    return false;
  }
  std::string frame = "fUzIoN";
  put32(frame, 10 + cmd.length() + 4);
  frame += cmd.c_str();
  put32(frame, crc32((const uint8_t*)frame.data(), frame.size()));

  xSemaphoreTake(hintSem, 0);  // drop stale hints
  size_t chunk = client->getMTU() - 3;
  for (size_t o = 0; o < frame.size(); o += chunk) {
    if (!bamkeyChar->writeValue((const uint8_t*)frame.data() + o, std::min(chunk, frame.size() - o), false)) {
      resp = "write failed";
      return false;
    }
  }
  if (xSemaphoreTake(hintSem, pdMS_TO_TICKS(kHintTimeoutMs)) != pdTRUE) {
    resp = "no response";
    return false;
  }

  std::string buf;
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    std::string part = bamkeyChar->readValue();
    if (part.empty()) {
      resp = "empty read";
      return false;
    }
    buf += part;
    if (buf.size() < 10) continue;
    uint32_t total = get32(buf, 6);
    if (buf.compare(0, 6, "fUzIoN") != 0 || total < 14 || total > kMaxFrame || buf.size() > total) {
      resp = "bad frame";
      return false;
    }
    if (buf.size() < total) continue;
    if (crc32((const uint8_t*)buf.data(), total - 4) != get32(buf, total - 4)) {
      resp = "bad CRC";
      return false;
    }
    std::string payload = buf.substr(10, total - 14);
    sanitize(&payload[0]);
    resp = String(payload.c_str());
    resp.trim();
    lastLinkOk = true;  // a valid frame came back, whatever the bed answered
    if (!resp.startsWith("PASS:")) return false;  // e.g. "FAIL:0" = unknown command
    resp.remove(0, 5);
    if (resp.startsWith("ACK")) resp.remove(0, 3);
    resp.trim();
    return true;
  }
  resp = "read timeout";
  return false;
}

void addLog(const String& cmd, const String& resp, uint32_t ms, bool ok) {
  mutate([&](BedState& s) {
    LogEntry& e = s.log[s.logHead];
    strlcpy(e.cmd, cmd.c_str(), sizeof(e.cmd));
    strlcpy(e.resp, resp.length() ? resp.c_str() : "ok", sizeof(e.resp));
    e.ms = ms > 65535 ? 65535 : ms;
    e.ok = ok;
    s.logHead = (s.logHead + 1) % kLogSize;
    if (s.logCount < kLogSize) s.logCount++;
    if (ok) s.cmdOk++;
    else {
      s.cmdFail++;
      snprintf(s.lastError, sizeof(s.lastError), "%s: %s", cmd.c_str(), resp.c_str());
    }
  });
}

// Polls are not logged (they'd drown the commands) unless they fail.
bool bamkey(const String& cmd, String* out = nullptr, bool logIt = false) {
  String resp;
  uint32_t t0 = millis();
  bool ok = bamkeyRaw(cmd, resp);
  if (logIt || !ok) addLog(cmd, resp, millis() - t0, ok);
  if (!ok) Serial.printf("[bed] %s -> %s\n", cmd.c_str(), resp.c_str());
  if (ok && out) *out = resp;
  return ok;
}

// Split a space separated response into at most `max` tokens.
int tokens(const String& s, String* outTok, int max) {
  int n = 0, start = 0;
  while (n < max && start < (int)s.length()) {
    int sp = s.indexOf(' ', start);
    if (sp < 0) sp = s.length();
    if (sp > start) outTok[n++] = s.substring(start, sp);
    start = sp + 1;
  }
  return n;
}

// ---- Connection ------------------------------------------------------------

void nameFromAddr(const char* addr, char* out, size_t n) {
  // "12:34:56:78:9a:bc" -> "Smart bed 789ABC" (how the bed advertises itself)
  char tail[7] = "";
  size_t len = strlen(addr);
  if (len >= 8) {
    const char* p = addr + len - 8;
    snprintf(tail, sizeof(tail), "%c%c%c%c%c%c", p[0], p[1], p[3], p[4], p[6], p[7]);
    for (char& c : tail) c = toupper(c);
  }
  snprintf(out, n, "Smart bed %s", tail);
}

void disconnectBed() {
  if (client && client->isConnected()) client->disconnect();
  ready = false;
  if (BedLink::snapshot().connectedAt) mutate([](BedState& s) { s.connectedAt = 0; });
}

void loadTarget() {
  RemoteSettings& cfg = Settings::get();
  // Paired before the address was stored in settings: adopt the bonded bed.
  if (!cfg.bedAddr[0] && NimBLEDevice::getNumBonds() > 0) {
    NimBLEAddress bonded = NimBLEDevice::getBondedAddress(0);
    strlcpy(cfg.bedAddr, bonded.toString().c_str(), sizeof(cfg.bedAddr));
    cfg.bedAddrType = bonded.getType();
    Settings::save();
  }
  haveAddr = strlen(cfg.bedAddr) > 0;
  if (haveAddr) bedAddr = NimBLEAddress(std::string(cfg.bedAddr), cfg.bedAddrType);
  mutate([&](BedState& s) {
    strlcpy(s.bedAddr, cfg.bedAddr, sizeof(s.bedAddr));
    if (haveAddr) nameFromAddr(cfg.bedAddr, s.bedName, sizeof(s.bedName));
    else s.bedName[0] = 0;
    s.bonds = NimBLEDevice::getNumBonds();
  });
}

void doScan() {
  disconnectBed();
  mutate([](BedState& s) {
    s.scanning = true;
    s.foundCount = 0;
  });
  setLink(LinkState::Scanning, "Searching for beds");
  vTaskDelay(pdMS_TO_TICKS(300));  // let a just-released bed start advertising
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->start(6, false);
  mutate([&](BedState& s) {
    for (int i = 0; i < results.getCount() && s.foundCount < kMaxFound; i++) {
      NimBLEAdvertisedDevice d = results.getDevice(i);
      if (!d.isAdvertisingService(kService)) continue;
      FoundBed& f = s.found[s.foundCount++];
      strlcpy(f.addr, d.getAddress().toString().c_str(), sizeof(f.addr));
      strlcpy(f.name, d.getName().c_str(), sizeof(f.name));
      sanitize(f.name);
      if (!f.name[0]) nameFromAddr(f.addr, f.name, sizeof(f.name));
      f.rssi = d.getRSSI();
      f.addrType = d.getAddress().getType();
    }
    s.scanning = false;
  });
  scan->clearResults();
  setLink(LinkState::Connecting, "Scan finished");
}

void doPair(const FoundBed& target) {
  disconnectBed();
  RemoteSettings& cfg = Settings::get();
  strlcpy(cfg.bedAddr, target.addr, sizeof(cfg.bedAddr));
  cfg.bedAddrType = target.addrType;
  Settings::save();
  loadTarget();
  NimBLEDevice::deleteBond(bedAddr);  // force a fresh pairing
  authFailures = 0;
  mutate([](BedState& s) {
    s.configLoaded = false;
    s.bonds = NimBLEDevice::getNumBonds();
  });
  setLink(LinkState::NeedsPairing, "Waiting for pairing mode");
}

void doForget() {
  disconnectBed();
  NimBLEDevice::deleteAllBonds();
  mutate([](BedState& s) { s.bonds = 0; });
  setLink(LinkState::NeedsPairing, "Pairing needed");
}

void loadConfig() {
  String r, t[16];
  if (bamkey("SYCG ", &r, true) && tokens(r, t, 16) == 16) {
    int wr = screenSide(false), wl = screenSide(true);
    mutate([&](BedState& st) {
      strlcpy(st.rawConfig, r.c_str(), sizeof(st.rawConfig));
      st.hasPressure = t[1] == "yes";
      st.hasArticulation = t[2] == "yes";
      st.hasLight = t[3] == "yes";
      st.hasFootwarming = t[4] == "yes";
      strlcpy(st.thermal, t[5].c_str(), sizeof(st.thermal));
      st.hasActuator[wr][ACT_HEAD] = t[6] == "yes";
      st.hasActuator[wr][ACT_FOOT] = t[7] == "yes";
      st.hasActuator[wl][ACT_HEAD] = t[8] == "yes";
      st.hasActuator[wl][ACT_FOOT] = t[9] == "yes";
      st.presetFlat = t[10] == "yes";
      st.presetFavorite = t[11] == "yes";
      st.presetSnore = t[12] == "yes";
      st.presetZeroG = t[13] == "yes";
      st.presetTv = t[14] == "yes";
      st.presetRead = t[15] == "yes";
      st.configLoaded = true;
    });
  }
  if (bamkey("SYAG ", &r, true)) mutate([&](BedState& st) { strlcpy(st.model, r.c_str(), sizeof(st.model)); });
  for (int s = 0; s < 2; s++) {
    if (bamkey(String("SNFG ") + wire(s), &r)) mutate([&](BedState& st) { st.favorite[s] = r.toInt(); });
  }
}

bool connectBed() {
  setLink(LinkState::Connecting, "Connecting");
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setConnectTimeout(8);
  }
  if (!client->connect(bedAddr)) {
    setLink(LinkState::Error, "Bed out of range");
    return false;
  }
  NimBLERemoteService* svc = client->getService(kService);
  NimBLERemoteCharacteristic* auth = svc ? svc->getCharacteristic(kAuth) : nullptr;
  bamkeyChar = svc ? svc->getCharacteristic(kBamkey) : nullptr;
  if (!auth || !bamkeyChar) {
    client->disconnect();
    setLink(LinkState::Error, "Not a Fuzion bed");
    return false;
  }

  // Bond (encrypt with stored keys, or pair if the bed is in pairing mode).
  bool bonded = NimBLEDevice::isBonded(bedAddr);
  if (!client->secureConnection()) {
    client->disconnect();
    // The bed may have forgotten us; drop our stale keys so a fresh pairing can succeed.
    if (bonded && ++authFailures >= 3) {
      NimBLEDevice::deleteBond(bedAddr);
      authFailures = 0;
    }
    setLink(LinkState::NeedsPairing, "Pairing needed");
    return false;
  }

  std::string sid;
  for (int i = 0; i < 3 && sid.size() != 16; i++) sid = auth->readValue();
  static const uint8_t zero[16] = {0};
  if (sid.size() == 16 && memcmp(sid.data(), zero, 16) == 0) {
    client->disconnect();
    setLink(LinkState::Busy, "Bed busy (phone app?)");
    return false;
  }
  if (sid.size() != 16 || (sid[15] == 1 && memcmp(sid.data(), zero, 15) == 0)) {
    client->disconnect();
    setLink(LinkState::NeedsPairing, "Pairing needed");
    return false;
  }
  memcpy(session, sid.data(), 16);
  if (!bamkeyChar->subscribe(true, onNotify)) {
    client->disconnect();
    setLink(LinkState::Error, "Subscribe failed");
    return false;
  }
  authFailures = 0;
  cmdFailures = 0;
  mutate([](BedState& s) {
    s.connectedAt = millis();
    s.mtu = client->getMTU();
    s.rssi = client->getRssi();
    s.bonds = NimBLEDevice::getNumBonds();
  });
  loadConfig();
  pollIdx = 0;
  nextPollAt = 0;
  setLink(LinkState::Ready, "Connected");
  return true;
}

// ---- Polling ---------------------------------------------------------------

constexpr int kPollItems = 9;

// Reconnect after repeated transport failures. A bed "FAIL:" reply (e.g. an
// unsupported command) means the link itself is fine.
void noteResult() {
  if (lastLinkOk) {
    cmdFailures = 0;
  } else if (++cmdFailures >= 3) {
    Serial.println("[bed] too many failures, reconnecting");
    disconnectBed();
  }
}

// Poll one value per call so queued commands are never stuck behind a full cycle.
void pollStep() {
  String r, t[3];
  BedState st = BedLink::snapshot();
  int i = pollIdx;
  pollIdx = (pollIdx + 1) % kPollItems;

  if (i < 2) {  // SNCG <side> -> "<adjusting> <ambient> <user>"
    bool ok = bamkey(String("SNCG ") + wire(i), &r) && tokens(r, t, 3) == 3;
    if (ok) mutate([&](BedState& s) {
        s.adjusting[i] = t[0] == "1";
        s.ambient[i] = t[1].toInt();
        s.sleepNumber[i] = t[2].toInt();
      });
    noteResult();
  } else if (i < 6) {  // ACTG <side> <head|foot>
    int side = (i - 2) / 2, act = (i - 2) % 2;
    if (st.hasActuator[side][act]) {
      bool ok = bamkey(String("ACTG ") + wire(side) + " " + kActName[act], &r);
      if (ok) {
        int v = r.toInt();
        if (st.position[side][act] >= 0 && st.position[side][act] != v) sawMotion = true;
        mutate([&](BedState& s) { s.position[side][act] = v; });
      }
      noteResult();
    }
  } else if (i < 8) {  // AGCP <side>
    int side = i - 6;
    bool ok = bamkey(String("AGCP ") + wire(side), &r);
    if (ok) mutate([&](BedState& s) { strlcpy(s.preset[side], r.c_str(), sizeof(s.preset[side])); });
    noteResult();
  } else if (st.hasLight) {  // UBLG -> "<level> <timer>"
    bool ok = bamkey("UBLG ", &r) && tokens(r, t, 2) >= 1;
    if (ok) mutate([&](BedState& s) {
        s.lightOn = t[0] != "off";
        strlcpy(s.lightRaw, r.c_str(), sizeof(s.lightRaw));
      });
    noteResult();
  }

  if (pollIdx == 0 && ready) {  // finished a cycle
    st = BedLink::snapshot();
    bool moving = sawMotion || millis() - lastMotionCmdAt < kMotionGraceMs;
    bool busy = moving || st.adjusting[0] || st.adjusting[1] || millis() < fastUntil;
    sawMotion = false;
    mutate([&](BedState& s) {
      s.moving = moving;
      s.rssi = client->getRssi();
    });
    nextPollAt = millis() + (busy ? kPollFastMs : kPollSlowMs);
  }
}

void execute(const BedCmd& c) {
  const char* side = wire(c.side);
  String cmd;
  switch (c.type) {
    case CmdType::SetSleepNumber: cmd = String("PSNS ") + side + " " + c.value; break;
    case CmdType::SetFavorite: cmd = String("SNFS ") + side + " " + c.value; break;
    case CmdType::Preset: cmd = String("ASTP ") + side + " " + c.preset; break;
    case CmdType::SetActuator: cmd = String("ACTS ") + side + " " + kActName[c.actuator] + " " + c.value; break;
    case CmdType::HaltAll: cmd = "ACHA "; break;
    case CmdType::Light: cmd = String("UBLS ") + (c.value ? "high " : "off ") + LIGHT_TIMER_MIN; break;
  }
  bool ok = bamkey(cmd, nullptr, true);
  Serial.printf("[bed] %s -> %s\n", cmd.c_str(), ok ? "ok" : "FAILED");
  if (ok) {
    if (c.type == CmdType::Preset || c.type == CmdType::SetActuator) {
      lastMotionCmdAt = millis();
      mutate([](BedState& s) { s.moving = true; });
    }
    if (c.type == CmdType::HaltAll) {
      lastMotionCmdAt = 0;
      mutate([](BedState& s) { s.moving = false; });
    }
    if (c.type == CmdType::SetFavorite) mutate([&](BedState& s) { s.favorite[c.side] = c.value; });
    if (c.type == CmdType::Light) mutate([&](BedState& s) { s.lightOn = c.value; });
  }
  noteResult();
}

void linkTask(void*) {
  for (;;) {
    if (reqForget) {
      reqForget = false;
      doForget();
    }
    if (reqPair) {
      reqPair = false;
      doPair(pairTarget);
    }
    if (reqScan) {
      reqScan = false;
      doScan();
    }
    if (reqReconnect) {
      reqReconnect = false;
      disconnectBed();
      loadTarget();
    }

    if (!active) {
      disconnectBed();
      xQueueReset(cmdQueue);  // never replay stale taps later
      setLink(LinkState::Asleep, "Asleep");
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!haveAddr) {
      setLink(LinkState::NoBed, "No bed paired");
      waitMs(500);
      continue;
    }
    if (!ready || !client->isConnected()) {
      if (ready) disconnectBed();
      ready = connectBed();
      if (!ready) waitMs(2500);
      continue;
    }
    BedCmd c;
    if (xQueueReceive(cmdQueue, &c, pdMS_TO_TICKS(20)) == pdTRUE) {
      execute(c);
      fastUntil = millis() + kFastAfterCmdMs;
      nextPollAt = millis() + 300;
      continue;
    }
    if ((int32_t)(millis() - nextPollAt) >= 0) pollStep();
  }
}

}  // namespace

namespace BedLink {

void begin() {
  stateMutex = xSemaphoreCreateMutex();
  hintSem = xSemaphoreCreateBinary();
  cmdQueue = xQueueCreate(8, sizeof(BedCmd));
  NimBLEDevice::init("Bed Remote");
  NimBLEDevice::setSecurityAuth(true, false, true);  // bond, no MITM, LE secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  Serial.printf("[bed] stored bonds: %d\n", NimBLEDevice::getNumBonds());
  loadTarget();
  xTaskCreatePinnedToCore(linkTask, "bedlink", 8192, nullptr, 2, nullptr, 0);
}

void setActive(bool a) { active = a; }

bool send(const BedCmd& cmd) { return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE; }

BedState snapshot() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  BedState copy = state;
  xSemaphoreGive(stateMutex);
  return copy;
}

void startScan() { reqScan = true; }

void pairWith(const FoundBed& bed) {
  pairTarget = bed;
  reqPair = true;
}

void forgetPairing() { reqForget = true; }

void reconnect() { reqReconnect = true; }

}  // namespace BedLink
