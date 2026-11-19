// Local HTTP API (see net_api.h). A deliberately small HTTP/1.1 server: one
// request per connection, read with a short timeout, answered and closed. The
// request buffers are allocated per request so the API costs no RAM while idle.

#include "net_api.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <mdns.h>

#include "bed_link.h"
#include "config.h"

namespace {

WiFiServer server(NetApi::kPort);
volatile bool wantOn = true;
bool haveWifi = false;
bool listening = false;
char names[2][12];

constexpr size_t kMaxRequest = 1536;  // headers + body
constexpr size_t kMaxReply = 1280;
constexpr uint32_t kReadTimeoutMs = 1000;

struct Ctx {
  char req[kMaxRequest + 1];
  char reply[kMaxReply];
  char method[8];
  char path[48];
  const char* query;  // after '?', or ""
  const char* body;   // after the headers, or ""
  BedState bed;
};

// ---- Parsing ---------------------------------------------------------------------

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c |= 0x20;
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

void urlDecode(const char* s, const char* end, char* out, size_t n) {
  size_t i = 0;
  while (s < end && i + 1 < n) {
    if (*s == '+') {
      out[i++] = ' ';
      s++;
    } else if (*s == '%' && end - s >= 3 && hexVal(s[1]) >= 0 && hexVal(s[2]) >= 0) {
      out[i++] = (char)(hexVal(s[1]) * 16 + hexVal(s[2]));
      s += 3;
    } else {
      out[i++] = *s++;
    }
  }
  out[i] = 0;
}

// key=value&key=value (query strings and form bodies)
bool formParam(const char* s, const char* key, char* out, size_t n) {
  size_t kl = strlen(key);
  while (s && *s) {
    const char* amp = strchr(s, '&');
    const char* end = amp ? amp : s + strlen(s);
    const char* eq = (const char*)memchr(s, '=', end - s);
    size_t len = (eq ? eq : end) - s;
    if (len == kl && strncasecmp(s, key, kl) == 0) {
      urlDecode(eq ? eq + 1 : end, end, out, n);
      return true;
    }
    s = amp ? amp + 1 : nullptr;
  }
  return false;
}

// {"key": "value"} or {"key": 40} / true / false. Flat objects only.
bool jsonParam(const char* s, const char* key, char* out, size_t n) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  for (const char* p = strstr(s, pat); p; p = strstr(p + 1, pat)) {
    const char* v = p + strlen(pat);
    while (isspace((unsigned char)*v)) v++;
    if (*v != ':') continue;
    v++;
    while (isspace((unsigned char)*v)) v++;
    size_t i = 0;
    if (*v == '"') {
      for (v++; *v && *v != '"' && i + 1 < n; v++) {
        if (*v == '\\' && v[1]) v++;
        out[i++] = *v;
      }
    } else {
      while (*v && *v != ',' && *v != '}' && !isspace((unsigned char)*v) && i + 1 < n) out[i++] = *v++;
    }
    out[i] = 0;
    return true;
  }
  return false;
}

bool param(const Ctx& c, const char* key, char* out, size_t n) {
  out[0] = 0;
  if (formParam(c.query, key, out, n)) return true;
  const char* b = c.body;
  while (isspace((unsigned char)*b)) b++;
  return *b == '{' ? jsonParam(b, key, out, n) : formParam(b, key, out, n);
}

bool parseInt(const char* s, int& v) {
  if (!*s) return false;
  char* end;
  long l = strtol(s, &end, 10);
  if (*end) return false;
  v = (int)l;
  return true;
}

bool parseBool(const char* s, bool& v) {
  static const char* const yes[] = {"1", "true", "on", "yes"};
  static const char* const no[] = {"0", "false", "off", "no"};
  for (const char* y : yes)
    if (!strcasecmp(s, y)) return v = true, true;
  for (const char* x : no)
    if (!strcasecmp(s, x)) return v = false, true;
  return false;
}

// Bit 0 = left, bit 1 = right (as seen on the remote's screen); 0 = not a side.
int parseSides(const char* s) {
  if (!*s || !strcasecmp(s, "both") || !strcasecmp(s, "all")) return 3;
  if (!strcasecmp(s, "left") || !strcmp(s, "0")) return 1;
  if (!strcasecmp(s, "right") || !strcmp(s, "1")) return 2;
  for (int i = 0; i < 2; i++)
    if (names[i][0] && !strcasecmp(s, names[i])) return 1 << i;
  return 0;
}

struct PresetName {
  const char* alias;
  const char* wire;
};
const PresetName kPresetNames[] = {{"flat", "flat"},         {"zero_g", "zero_g"},     {"zerog", "zero_g"},
                                   {"snore", "snore"},       {"anti_snore", "snore"},  {"favorite", "favorite"},
                                   {"favourite", "favorite"}, {"watch_tv", "watch_tv"}, {"tv", "watch_tv"},
                                   {"read", "read"},         {"reading", "read"}};

const char* presetWire(const char* s) {
  char n[16];
  size_t i = 0;
  for (; s[i] && i + 1 < sizeof(n); i++) n[i] = s[i] == ' ' || s[i] == '-' ? '_' : (char)tolower((unsigned char)s[i]);
  n[i] = 0;
  for (const PresetName& p : kPresetNames)
    if (!strcmp(n, p.alias)) return p.wire;
  return nullptr;
}

bool bedHasPreset(const BedState& b, const char* wire) {
  if (!b.configLoaded) return true;  // not read yet: let the bed decide
  if (!strcmp(wire, "flat")) return b.presetFlat;
  if (!strcmp(wire, "zero_g")) return b.presetZeroG;
  if (!strcmp(wire, "snore")) return b.presetSnore;
  if (!strcmp(wire, "favorite")) return b.presetFavorite;
  if (!strcmp(wire, "watch_tv")) return b.presetTv;
  if (!strcmp(wire, "read")) return b.presetRead;
  return false;
}

// ---- Replies -----------------------------------------------------------------------

void send(WiFiClient& cl, int code, const char* json) {
  const char* reason = code == 200   ? "OK"
                       : code == 204 ? "No Content"
                       : code == 400 ? "Bad Request"
                       : code == 404 ? "Not Found"
                       : code == 405 ? "Method Not Allowed"
                       : code == 413 ? "Payload Too Large"
                       : code == 503 ? "Service Unavailable"
                                     : "Error";
  cl.printf(
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: application/json\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Cache-Control: no-store\r\n"
      "Connection: close\r\n"
      "Content-Length: %u\r\n\r\n",
      code, reason, (unsigned)strlen(json));
  cl.print(json);
}

void fail(WiFiClient& cl, Ctx& c, int code, const char* msg) {
  snprintf(c.reply, sizeof(c.reply), "{\"ok\":false,\"error\":\"%s\"}", msg);
  send(cl, code, c.reply);
}

// Appends a JSON string (or null for an empty one) at out.
size_t jsonStr(char* out, size_t n, const char* s, bool emptyIsNull = false) {
  if (emptyIsNull && !*s) return snprintf(out, n, "null");
  size_t i = 0;
  if (i + 1 < n) out[i++] = '"';
  for (; *s && i + 3 < n; s++) {
    unsigned char ch = *s;
    if (ch == '"' || ch == '\\') {
      out[i++] = '\\';
      out[i++] = ch;
    } else if (ch >= 0x20) {
      out[i++] = ch;
    }
  }
  if (i + 1 < n) out[i++] = '"';
  out[i < n ? i : n - 1] = 0;
  return i;
}

const char* linkName(LinkState s) {
  switch (s) {
    case LinkState::Asleep: return "asleep";
    case LinkState::NoBed: return "no_bed";
    case LinkState::Scanning: return "scanning";
    case LinkState::Connecting: return "connecting";
    case LinkState::NeedsPairing: return "needs_pairing";
    case LinkState::Busy: return "busy";
    case LinkState::Ready: return "ready";
    case LinkState::Error: return "error";
  }
  return "unknown";
}

// Writes a number, or null for the bed's "unknown" (-1).
size_t num(char* out, size_t n, int v) { return v < 0 ? snprintf(out, n, "null") : snprintf(out, n, "%d", v); }

void status(WiFiClient& cl, Ctx& c) {
  const BedState& b = c.bed;
  char* o = c.reply;
  size_t n = sizeof(c.reply), i = 0;
  auto put = [&](size_t w) { i = i + w < n ? i + w : n - 1; };

  put(snprintf(o + i, n - i, "{\"ok\":true,\"device\":\"Bed Remote\",\"firmware\":\"%s\",\"link\":\"%s\",\"connected\":%s,",
               FW_VERSION, linkName(b.link), b.connectedAt ? "true" : "false"));
  put(snprintf(o + i, n - i, "\"moving\":%s,\"model\":", b.moving ? "true" : "false"));
  put(jsonStr(o + i, n - i, b.model, true));
  put(snprintf(o + i, n - i, ",\"presets\":["));
  bool first = true;
  const PresetName* seen[8];
  int seenCount = 0;
  for (const PresetName& p : kPresetNames) {
    bool dup = false;
    for (int k = 0; k < seenCount; k++) dup |= !strcmp(seen[k]->wire, p.wire);
    if (dup || !bedHasPreset(b, p.wire)) continue;
    seen[seenCount++] = &p;
    put(snprintf(o + i, n - i, "%s\"%s\"", first ? "" : ",", p.wire));
    first = false;
  }
  put(snprintf(o + i, n - i, "],\"light\":"));
  if (b.configLoaded && !b.hasLight) put(snprintf(o + i, n - i, "null"));
  else put(snprintf(o + i, n - i, "{\"on\":%s}", b.lightOn ? "true" : "false"));
  put(snprintf(o + i, n - i, ",\"sides\":["));
  for (int s = 0; s < 2; s++) {
    put(snprintf(o + i, n - i, "%s{\"side\":\"%s\",\"name\":", s ? "," : "", s ? "right" : "left"));
    put(jsonStr(o + i, n - i, names[s]));
    put(snprintf(o + i, n - i, ",\"sleepNumber\":"));
    put(num(o + i, n - i, b.sleepNumber[s]));
    put(snprintf(o + i, n - i, ",\"favorite\":"));
    put(num(o + i, n - i, b.favorite[s]));
    put(snprintf(o + i, n - i, ",\"preset\":"));
    put(jsonStr(o + i, n - i, b.preset[s], true));
    put(snprintf(o + i, n - i, ",\"head\":"));
    put(num(o + i, n - i, b.position[s][ACT_HEAD]));
    put(snprintf(o + i, n - i, ",\"foot\":"));
    put(num(o + i, n - i, b.position[s][ACT_FOOT]));
    put(snprintf(o + i, n - i, "}"));
  }
  put(snprintf(o + i, n - i, "]}"));
  send(cl, 200, o);
}

void apiIndex(WiFiClient& cl) {
  send(cl, 200,
       "{\"ok\":true,\"device\":\"Bed Remote\",\"firmware\":\"" FW_VERSION
       "\",\"endpoints\":{"
       "\"GET /api/status\":\"bed state; ?refresh=1 wakes the bed link first\","
       "\"/api/preset\":\"side=left|right|both|<name>, preset=flat|zero_g|snore|favorite|watch_tv|read\","
       "\"/api/sleep-number\":\"side, value=5..100 (rounded to 5)\","
       "\"/api/position\":\"side, actuator=head|foot, value=0..100\","
       "\"/api/light\":\"on=true|false\","
       "\"/api/stop\":\"stop all motion\"},"
       "\"notes\":\"Commands accept GET or POST, with query, form or JSON parameters. left/right are as on the remote's "
       "screen.\"}");
}

// Queues cmd for each side in mask; false if the queue is full.
bool queue(BedCmd cmd, int mask) {
  bool ok = true;
  for (int s = 0; s < 2; s++)
    if (mask & (1 << s)) {
      cmd.side = (BedSide)s;
      ok &= BedLink::send(cmd);
    }
  return ok;
}

void command(WiFiClient& cl, Ctx& c, const char* what) {
  char v[32];
  BedCmd cmd;
  int mask = 3;
  if (param(c, "side", v, sizeof(v)) && !(mask = parseSides(v))) return fail(cl, c, 400, "side must be left, right, both or a side's name");

  if (!strcmp(what, "preset")) {
    param(c, "preset", v, sizeof(v)) || param(c, "name", v, sizeof(v));
    const char* wire = presetWire(v);
    if (!wire) return fail(cl, c, 400, "preset must be flat, zero_g, snore, favorite, watch_tv or read");
    if (!bedHasPreset(c.bed, wire)) return fail(cl, c, 400, "this bed doesn't have that preset");
    cmd.type = CmdType::Preset;
    strlcpy(cmd.preset, wire, sizeof(cmd.preset));
  } else if (!strcmp(what, "sleep-number")) {
    int n;
    if (!param(c, "value", v, sizeof(v)) || !parseInt(v, n) || n < 0 || n > 100)
      return fail(cl, c, 400, "value must be a number from 5 to 100");
    cmd.type = CmdType::SetSleepNumber;
    cmd.value = constrain((n + 2) / 5 * 5, 5, 100);
  } else if (!strcmp(what, "position")) {
    int n;
    param(c, "actuator", v, sizeof(v));
    if (!strcasecmp(v, "head")) cmd.actuator = ACT_HEAD;
    else if (!strcasecmp(v, "foot")) cmd.actuator = ACT_FOOT;
    else return fail(cl, c, 400, "actuator must be head or foot");
    if (!param(c, "value", v, sizeof(v)) || !parseInt(v, n) || n < 0 || n > 100)
      return fail(cl, c, 400, "value must be a number from 0 to 100");
    for (int s = 0; s < 2; s++)  // skip sides without that motor
      if (c.bed.configLoaded && !c.bed.hasActuator[s][cmd.actuator]) mask &= ~(1 << s);
    if (!mask) return fail(cl, c, 400, "this bed has no such motor on that side");
    cmd.type = CmdType::SetActuator;
    cmd.value = n;
  } else if (!strcmp(what, "light")) {
    bool on;
    if (!param(c, "on", v, sizeof(v)) || !parseBool(v, on)) return fail(cl, c, 400, "on must be true or false");
    if (c.bed.configLoaded && !c.bed.hasLight) return fail(cl, c, 400, "this bed has no light");
    cmd.type = CmdType::Light;
    cmd.value = on;
    mask = 1;  // one command for the whole bed
  } else if (!strcmp(what, "stop")) {
    cmd.type = CmdType::HaltAll;
    mask = 1;
  } else {
    return fail(cl, c, 404, "no such endpoint, see /api");
  }

  if (!queue(cmd, mask)) return fail(cl, c, 503, "busy, try again");
  Serial.printf("[api] %s\n", what);
  send(cl, 200, c.bed.connectedAt ? "{\"ok\":true}" : "{\"ok\":true,\"note\":\"bed link waking up, takes a few seconds\"}");
}

// ---- Server ------------------------------------------------------------------------

// Reads one request into c.req. Returns 0, or an HTTP error code.
int readRequest(WiFiClient& cl, Ctx& c) {
  size_t len = 0, need = 0;
  char* body = nullptr;
  uint32_t start = millis();
  while (millis() - start < kReadTimeoutMs) {
    int avail = cl.available();
    if (avail <= 0) {
      if (!cl.connected()) break;
      delay(2);
      continue;
    }
    if (len >= kMaxRequest) return 413;
    len += cl.read((uint8_t*)c.req + len, min((size_t)avail, kMaxRequest - len));
    c.req[len] = 0;
    if (!body && (body = strstr(c.req, "\r\n\r\n"))) {
      body += 4;
      const char* lenHdr = strcasestr(c.req, "\ncontent-length:");
      need = (body - c.req) + (lenHdr ? strtoul(lenHdr + 16, nullptr, 10) : 0);
      if (need > kMaxRequest) return 413;
    }
    if (body && len >= need) break;
  }
  if (!body) return 400;
  c.body = body;
  // Request line, split in place: METHOD SP target SP HTTP/1.x
  char* sp1 = strchr(c.req, ' ');
  if (!sp1 || sp1 - c.req >= (int)sizeof(c.method)) return 400;
  char* target = sp1 + 1;
  char* sp2 = strpbrk(target, " \r\n");
  if (!sp2 || sp2 >= body) return 400;
  *sp1 = *sp2 = 0;
  strlcpy(c.method, c.req, sizeof(c.method));
  char* q = strchr(target, '?');
  if (q) *q++ = 0;
  c.query = q ? q : "";
  strlcpy(c.path, target, sizeof(c.path));
  size_t pl = strlen(c.path);
  while (pl > 1 && c.path[pl - 1] == '/') c.path[--pl] = 0;
  return 0;
}

void handle(WiFiClient& cl) {
  Ctx* c = (Ctx*)malloc(sizeof(Ctx));
  if (!c) {
    cl.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    return;
  }
  int err = readRequest(cl, *c);
  if (err) {
    fail(cl, *c, err, err == 413 ? "request too large" : "bad request");
  } else if (!strcmp(c->method, "OPTIONS")) {
    send(cl, 204, "");
  } else if (strcmp(c->method, "GET") && strcmp(c->method, "POST")) {
    fail(cl, *c, 405, "use GET or POST");
  } else {
    c->bed = BedLink::snapshot();
    char v[8];
    if (!strcmp(c->path, "/") || !strcmp(c->path, "/api")) {
      apiIndex(cl);
    } else if (!strcmp(c->path, "/api/status")) {
      bool refresh = false;
      if (param(*c, "refresh", v, sizeof(v)) && parseBool(v, refresh) && refresh) BedLink::wake();
      status(cl, *c);
    } else if (!strncmp(c->path, "/api/", 5)) {
      command(cl, *c, c->path + 5);
    } else {
      fail(cl, *c, 404, "no such endpoint, see /api");
    }
  }
  free(c);
}

void start() {
  server.begin();
  server.setNoDelay(true);
  MDNS.addService("http", "tcp", NetApi::kPort);
  MDNS.addServiceTxt("http", "tcp", "path", "/api");
  MDNS.addService("bedremote", "tcp", NetApi::kPort);
  MDNS.addServiceTxt("bedremote", "tcp", "path", "/api");
  MDNS.addServiceTxt("bedremote", "tcp", "fw", FW_VERSION);
  listening = true;
  Serial.printf("[api] http://%s.local:%u/api\n", NetApi::kHostName, NetApi::kPort);
}

void stop() {
  server.end();
  mdns_service_remove("_http", "_tcp");
  mdns_service_remove("_bedremote", "_tcp");
  listening = false;
  Serial.println("[api] off");
}

}  // namespace

namespace NetApi {

void setEnabled(bool on) { wantOn = on; }
bool enabled() { return wantOn; }

void wifiUp(const char n[2][12]) {
  memcpy(names, n, sizeof(names));
  haveWifi = true;
}

void poll() {
  if (!haveWifi) return;
  if (wantOn != listening) wantOn ? start() : stop();
  if (!listening) return;
  WiFiClient cl = server.available();
  if (!cl) return;
  cl.setNoDelay(true);
  handle(cl);
  cl.stop();
}

}  // namespace NetApi
