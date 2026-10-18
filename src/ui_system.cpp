// System screens: Settings page, pairing flow and the Bed info (diagnostics)
// overlay.

#include <Arduino.h>

#include "config.h"
#include "settings.h"
#include "ui.h"
#include "ui_internal.h"

namespace ui {

namespace {

// ---- Shared bits -------------------------------------------------------------

lv_obj_t* overlay(lv_obj_t* scr, const char* titleText, lv_event_cb_t onBack, lv_obj_t** content) {
  lv_obj_t* o = panel(scr, kBg, 0);
  lv_obj_set_size(o, kW, kH);
  lv_obj_t* back = button(o, LV_SYMBOL_LEFT, &lv_font_montserrat_20, kSurface);
  lv_obj_set_size(back, 48, 40);
  lv_obj_set_pos(back, 6, 4);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* t = text(o, titleText, &lv_font_montserrat_20, kText);
  lv_obj_set_pos(t, 66, 13);
  *content = box(o);
  lv_obj_set_size(*content, kW, kH - 50);
  lv_obj_set_pos(*content, 0, 50);
  setHidden(o, true);
  return o;
}

void makeScrollList(lv_obj_t* o) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(o, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_all(o, 6, 0);
  lv_obj_set_style_pad_row(o, 6, 0);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
}

lv_obj_t* listRow(lv_obj_t* parent, const char* icon, const char* label, lv_event_cb_t cb, int h = 48) {
  lv_obj_t* r = panel(parent, kSurface, 14);
  lv_obj_set_size(r, kW - 12, h);
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, kSurface2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* chev = text(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, kMuted);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
  }
  if (icon) {
    lv_obj_t* i = text(r, icon, &lv_font_montserrat_16, kAccent);
    lv_obj_align(i, LV_ALIGN_LEFT_MID, 14, 0);
  }
  lv_obj_t* l = text(r, label, &lv_font_montserrat_16, kText);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, icon ? 44 : 14, 0);
  return r;
}

void styleSwitch(lv_obj_t* sw) {
  lv_obj_set_size(sw, 52, 28);
  lv_obj_set_style_bg_color(sw, kSurface2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, kAccent, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_ext_click_area(sw, 10);
}

// ---- Settings page -------------------------------------------------------------

const uint16_t kOffOptions[] = {30, 60, 300, 0};
const char* const kOffLabels[] = {"30s", "1m", "5m", "Never"};

lv_obj_t* bedTitle;
lv_obj_t* bedSub;
lv_obj_t* namesSub;
lv_obj_t* swapSwitch;
lv_obj_t* flipSwitch;
lv_obj_t* brightSlider;
lv_obj_t* offBtn[4];

void onBedRow(lv_event_t*) { openInfo(); }
void onPairRow(lv_event_t*) { openPairing(true); }
void onNamesRow(lv_event_t*) { openNames(); }

void onSwap(lv_event_t*) {
  Settings::get().swapSides = lv_obj_has_state(swapSwitch, LV_STATE_CHECKED);
  Settings::save();
  BedLink::reconnect();  // re-read the bed so features land on the right side
}

void onFlip(lv_event_t*) {
  Settings::get().flipScreen = lv_obj_has_state(flipSwitch, LV_STATE_CHECKED);
  Settings::save();
  Platform::applyRotation();
}

void onBright(lv_event_t* e) {
  Settings::get().brightness = lv_slider_get_value(brightSlider);
  Platform::setBrightness(Settings::get().brightness);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) Settings::save();
}

void onOff(lv_event_t* e) {
  Settings::get().screenOffSec = kOffOptions[(intptr_t)lv_event_get_user_data(e)];
  Settings::save();
  refreshSettings();
}

void onForgetConfirm(lv_event_t* e) {
  lv_obj_t* mbox = lv_event_get_current_target(e);
  const char* btn = lv_msgbox_get_active_btn_text(mbox);
  if (btn && strcmp(btn, "Forget") == 0) BedLink::forgetPairing();
  lv_msgbox_close_async(mbox);  // not lv_msgbox_close: we are inside its own event
}

void onForget(lv_event_t*) {
  static const char* btns[] = {"Cancel", "Forget", ""};
  lv_obj_t* m = lv_msgbox_create(nullptr, "Forget pairing?",
                                 "To reconnect you'll need to hold the bed's pairing button.", btns, false);
  lv_obj_set_width(m, kW - 20);
  lv_obj_set_style_bg_color(m, kSurface, 0);
  lv_obj_set_style_radius(m, 18, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_text_color(m, kText, 0);
  lv_obj_t* title = lv_msgbox_get_title(m);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_t* btnm = lv_msgbox_get_btns(m);
  lv_obj_set_size(btnm, LV_PCT(100), 44);
  lv_obj_set_style_bg_color(btnm, kSurface2, LV_PART_ITEMS);
  lv_obj_set_style_text_color(btnm, kText, LV_PART_ITEMS);
  lv_obj_set_style_radius(btnm, 12, LV_PART_ITEMS);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, onForgetConfirm, LV_EVENT_VALUE_CHANGED, nullptr);
  // Dim the background behind the dialog.
  lv_obj_set_style_bg_opa(lv_obj_get_parent(m), LV_OPA_60, 0);
  lv_obj_set_style_bg_color(lv_obj_get_parent(m), lv_color_black(), 0);
}

// ---- Pairing overlay -------------------------------------------------------------

enum class PairStep { Scan, Wait, Done };
PairStep pairStep = PairStep::Scan;
bool pairSawNotReady = false;

lv_obj_t* pairOverlay;
lv_obj_t* scanView;
lv_obj_t* scanList;
lv_obj_t* scanStatus;
lv_obj_t* scanAgain;
lv_obj_t* scanSpinner;
lv_obj_t* waitView;
lv_obj_t* waitStatus;
lv_obj_t* doneView;
lv_obj_t* doneText;
uint32_t scanListVersion = UINT32_MAX;

void showPairStep(PairStep s) {
  pairStep = s;
  setHidden(scanView, s != PairStep::Scan);
  setHidden(waitView, s != PairStep::Wait);
  setHidden(doneView, s != PairStep::Done);
}

void onPairBack(lv_event_t*) { setHidden(pairOverlay, true); }
void onScanAgain(lv_event_t*) { BedLink::startScan(); }

void onFoundBed(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= bed.foundCount) return;
  BedLink::pairWith(bed.found[i]);
  pairSawNotReady = false;
  showPairStep(PairStep::Wait);
}

void rebuildScanList() {
  lv_obj_clean(scanList);
  for (int i = 0; i < bed.foundCount; i++) {
    const FoundBed& f = bed.found[i];
    bool current = strcasecmp(f.addr, bed.bedAddr) == 0;
    lv_obj_t* r = listRow(scanList, LV_SYMBOL_BLUETOOTH, f.name, onFoundBed, 54);
    if (current) lv_obj_set_style_text_color(lv_obj_get_child(r, 1), kOk, 0);  // the paired bed
    lv_obj_remove_event_cb(r, onFoundBed);
    lv_obj_add_event_cb(r, onFoundBed, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_align(lv_obj_get_child(r, 2), LV_ALIGN_TOP_LEFT, 40, 8);
    lv_obj_align(lv_obj_get_child(r, 1), LV_ALIGN_LEFT_MID, 12, 0);
    char sub[40];
    snprintf(sub, sizeof(sub), "%s  %d dBm", f.addr, f.rssi);
    lv_obj_t* s = text(r, sub, &lv_font_montserrat_12, kMuted);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 40, -8);
  }
}

void buildPairing(lv_obj_t* scr) {
  lv_obj_t* content;
  pairOverlay = overlay(scr, "Pair a bed", onPairBack, &content);

  // Step 1: pick a bed from the scan
  scanView = box(content);
  lv_obj_set_size(scanView, kW, kH - 50);
  scanStatus = text(scanView, "", &lv_font_montserrat_14, kMuted);
  lv_label_set_long_mode(scanStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(scanStatus, kW - 28);
  lv_obj_set_pos(scanStatus, 14, 0);
  scanList = box(scanView);
  lv_obj_set_size(scanList, kW, kH - 50 - 40 - 56);
  lv_obj_set_pos(scanList, 0, 40);
  makeScrollList(scanList);
  scanSpinner = lv_spinner_create(scanView, 1000, 60);
  lv_obj_set_size(scanSpinner, 48, 48);
  lv_obj_align(scanSpinner, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_style_arc_color(scanSpinner, kSurface2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(scanSpinner, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(scanSpinner, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(scanSpinner, 5, LV_PART_INDICATOR);
  scanAgain = button(scanView, LV_SYMBOL_REFRESH "  Scan again", &lv_font_montserrat_16, kSurface2);
  lv_obj_set_size(scanAgain, kW - 12, 44);
  lv_obj_set_pos(scanAgain, 6, kH - 50 - 52);
  lv_obj_add_event_cb(scanAgain, onScanAgain, LV_EVENT_CLICKED, nullptr);

  // Step 2: wait for the bed's pairing mode
  waitView = box(content);
  lv_obj_set_size(waitView, kW, kH - 50);
  lv_obj_t* spin = lv_spinner_create(waitView, 1200, 70);
  lv_obj_set_size(spin, 64, 64);
  lv_obj_align(spin, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_style_arc_color(spin, kSurface2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spin, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);
  lv_obj_t* bt = text(waitView, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_20, kAccent);
  lv_obj_align_to(bt, spin, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t* how = text(waitView, "Hold the pairing button on the bed until its light blinks blue.",
                       &lv_font_montserrat_16, kText);
  lv_label_set_long_mode(how, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(how, kW - 40);
  lv_obj_set_style_text_align(how, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(how, LV_ALIGN_TOP_MID, 0, 100);
  waitStatus = text(waitView, "", &lv_font_montserrat_14, kMuted);
  lv_obj_set_width(waitStatus, kW - 20);
  lv_obj_set_style_text_align(waitStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(waitStatus, LV_ALIGN_TOP_MID, 0, 190);

  // Step 3: done
  doneView = box(content);
  lv_obj_set_size(doneView, kW, kH - 50);
  lv_obj_t* ok = panel(doneView, kOk, LV_RADIUS_CIRCLE);
  lv_obj_set_size(ok, 64, 64);
  lv_obj_align(ok, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_center(text(ok, LV_SYMBOL_OK, &lv_font_montserrat_28, kBg));
  doneText = text(doneView, "", &lv_font_montserrat_16, kText);
  lv_label_set_long_mode(doneText, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(doneText, kW - 40);
  lv_obj_set_style_text_align(doneText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(doneText, LV_ALIGN_TOP_MID, 0, 104);
  lv_obj_t* done = button(doneView, "Done", &lv_font_montserrat_16, kAccent);
  lv_obj_set_size(done, kW - 12, 44);
  lv_obj_set_pos(done, 6, kH - 50 - 52);
  lv_obj_add_event_cb(done, onPairBack, LV_EVENT_CLICKED, nullptr);

  showPairStep(PairStep::Scan);
}

void refreshPairing() {
  if (lv_obj_has_flag(pairOverlay, LV_OBJ_FLAG_HIDDEN)) return;
  if (pairStep == PairStep::Scan) {
    uint32_t v = bed.foundCount * 2 + bed.scanning;
    if (v != scanListVersion) {
      scanListVersion = v;
      rebuildScanList();
    }
    if (bed.scanning) setText(scanStatus, "Searching for Sleep Number beds...");
    else if (bed.foundCount == 0) setText(scanStatus, "No beds found. Close the SleepIQ app and scan again.");
    else setText(scanStatus, "Tap your bed:");
    setHidden(scanSpinner, !bed.scanning);
    if (bed.scanning) lv_obj_add_state(scanAgain, LV_STATE_DISABLED);
    else lv_obj_clear_state(scanAgain, LV_STATE_DISABLED);
  } else if (pairStep == PairStep::Wait) {
    if (bed.link != LinkState::Ready) pairSawNotReady = true;
    if (bed.link == LinkState::Ready && pairSawNotReady) {
      setTextFmt(doneText, "Paired with %s", bed.bedName);
      showPairStep(PairStep::Done);
      return;
    }
    switch (bed.link) {
      case LinkState::NeedsPairing: setText(waitStatus, "Waiting for the bed..."); break;
      case LinkState::Busy: setText(waitStatus, "Bed busy - close the SleepIQ app"); break;
      case LinkState::Connecting: setText(waitStatus, "Connecting..."); break;
      default: setText(waitStatus, bed.status); break;
    }
  }
}

// ---- Bed info overlay -------------------------------------------------------------

lv_obj_t* infoOverlay;
lv_obj_t* infoList;

enum InfoKey {
  kiState, kiName, kiAddr, kiSignal, kiMtu, kiBonds, kiUptime, kiCmds, kiError,
  kiModel, kiFeatures, kiActuators, kiPresets, kiRaw, kiLight, kiCount
};
lv_obj_t* infoVal[kiCount];

enum SideKey { skNumber, skAir, skAdjusting, skFav, skHead, skFoot, skPreset, skCount };
lv_obj_t* sideVal[skCount][2];
lv_obj_t* logLabel;
lv_obj_t* infoSideHdr[2];

void onInfoBack(lv_event_t*) { setHidden(infoOverlay, true); }

lv_obj_t* section(const char* title) {
  lv_obj_t* t = text(infoList, title, &lv_font_montserrat_12, kAccent);
  lv_obj_set_style_pad_top(t, 4, 0);
  lv_obj_t* p = panel(infoList, kSurface, 14);
  lv_obj_set_size(p, kW - 12, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(p, 10, 0);
  lv_obj_set_style_pad_row(p, 6, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  return p;
}

constexpr int kKeyW = 88;                      // key column in Bed info
constexpr int kValW = kW - 12 - 20 - kKeyW;     // value column
constexpr int kSideColW = kValW / 2;

lv_obj_t* kv(lv_obj_t* sec, const char* key) {
  lv_obj_t* r = box(sec);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  text(r, key, &lv_font_montserrat_12, kMuted);
  lv_obj_t* v = text(r, "-", &lv_font_montserrat_14, kText);
  lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(v, kValW);
  lv_obj_set_x(v, kKeyW);
  return v;
}

void buildInfo(lv_obj_t* scr) {
  infoOverlay = overlay(scr, "Bed info", onInfoBack, &infoList);
  makeScrollList(infoList);

  lv_obj_t* s = section("CONNECTION");
  const char* connKeys[] = {"State", "Name", "Address", "Signal", "MTU", "Bonds", "Connected", "Commands", "Last error"};
  for (int i = kiState; i <= kiError; i++) infoVal[i] = kv(s, connKeys[i - kiState]);

  s = section("BED");
  const char* bedKeys[] = {"Model", "Features", "Motors", "Presets", "SYCG raw", "Light"};
  for (int i = kiModel; i <= kiLight; i++) infoVal[i] = kv(s, bedKeys[i - kiModel]);

  s = section("SIDES");
  lv_obj_t* hdr = box(s);
  lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
  for (int side = 0; side < 2; side++) {
    infoSideHdr[side] = text(hdr, sideName((BedSide)side), &lv_font_montserrat_14, kAccent);
    lv_obj_set_x(infoSideHdr[side], kKeyW + side * kSideColW);
  }
  const char* sideKeys[] = {"Sleep Number", "Air (actual)", "Pump", "Favorite", "Head", "Foot", "Preset"};
  for (int k = 0; k < skCount; k++) {
    lv_obj_t* r = box(s);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    text(r, sideKeys[k], &lv_font_montserrat_12, kMuted);
    for (int side = 0; side < 2; side++) {
      sideVal[k][side] = text(r, "-", &lv_font_montserrat_14, kText);
      lv_obj_set_x(sideVal[k][side], kKeyW + side * kSideColW);
    }
  }

  s = section("RECENT COMMANDS");
  logLabel = text(s, "", &lv_font_montserrat_12, kText);
  lv_label_set_recolor(logLabel, true);
  lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(logLabel, LV_PCT(100));

  lv_obj_t* foot = text(infoList, "Bed Remote " FW_VERSION, &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_pad_top(foot, 4, 0);
}

const char* linkName(LinkState s) {
  switch (s) {
    case LinkState::Asleep: return "Asleep";
    case LinkState::NoBed: return "No bed paired";
    case LinkState::Scanning: return "Scanning";
    case LinkState::Connecting: return "Connecting";
    case LinkState::NeedsPairing: return "Pairing needed";
    case LinkState::Busy: return "Busy (other device)";
    case LinkState::Ready: return "Connected";
    case LinkState::Error: return "Error";
  }
  return "?";
}

void setVal(int k, const char* v) { setText(infoVal[k], v && v[0] ? v : "-"); }

void refreshInfo() {
  if (lv_obj_has_flag(infoOverlay, LV_OBJ_FLAG_HIDDEN)) return;
  char b[96];
  setVal(kiState, linkName(bed.link));
  setVal(kiName, bed.bedName);
  setVal(kiAddr, bed.bedAddr);
  if (bed.link == LinkState::Ready) {
    const char* q = bed.rssi > -60 ? "excellent" : bed.rssi > -70 ? "good" : bed.rssi > -80 ? "fair" : "weak";
    snprintf(b, sizeof(b), "%d dBm (%s)", bed.rssi, q);
    setVal(kiSignal, b);
    uint32_t up = (millis() - bed.connectedAt) / 1000;
    snprintf(b, sizeof(b), "%lum %02lus", (unsigned long)(up / 60), (unsigned long)(up % 60));
    setVal(kiUptime, bed.connectedAt ? b : "-");
  } else {
    setVal(kiSignal, "-");
    setVal(kiUptime, "-");
  }
  snprintf(b, sizeof(b), "%u", bed.mtu);
  setVal(kiMtu, bed.mtu ? b : "-");
  snprintf(b, sizeof(b), "%u", bed.bonds);
  setVal(kiBonds, b);
  snprintf(b, sizeof(b), "%lu ok / %lu failed", (unsigned long)bed.cmdOk, (unsigned long)bed.cmdFail);
  setVal(kiCmds, b);
  setVal(kiError, bed.lastError[0] ? bed.lastError : "none");

  setVal(kiModel, bed.model);
  snprintf(b, sizeof(b), "%s%s%s%sclimate: %s", bed.hasPressure ? "air, " : "", bed.hasArticulation ? "base, " : "",
           bed.hasLight ? "light, " : "", bed.hasFootwarming ? "foot warming, " : "", bed.thermal[0] ? bed.thermal : "-");
  setVal(kiFeatures, bed.configLoaded ? b : "-");
  b[0] = 0;
  for (int s = 0; s < 2; s++)
    for (int a = 0; a < 2; a++)
      if (bed.hasActuator[s][a]) {
        if (b[0]) strlcat(b, ", ", sizeof(b));
        strlcat(b, sideName((BedSide)s), sizeof(b));
        strlcat(b, " ", sizeof(b));
        strlcat(b, a == ACT_HEAD ? "head" : "foot", sizeof(b));
      }
  setVal(kiActuators, bed.configLoaded ? b : "-");
  snprintf(b, sizeof(b), "%s%s%s%s%s%s", bed.presetFlat ? "flat " : "", bed.presetZeroG ? "zero_g " : "",
           bed.presetSnore ? "snore " : "", bed.presetFavorite ? "favorite " : "", bed.presetTv ? "watch_tv " : "",
           bed.presetRead ? "read" : "");
  setVal(kiPresets, bed.configLoaded ? b : "-");
  setVal(kiRaw, bed.rawConfig);
  setVal(kiLight, bed.hasLight ? bed.lightRaw : "not installed");

  for (int s = 0; s < 2; s++) {
    setText(infoSideHdr[s], sideName((BedSide)s));
    auto num = [&](int k, int v, const char* suffix) {
      if (v >= 0) setTextFmt(sideVal[k][s], "%d%s", v, suffix);
      else setText(sideVal[k][s], "-");
    };
    num(skNumber, bed.sleepNumber[s], "");
    num(skAir, bed.ambient[s], "");
    setText(sideVal[skAdjusting][s], bed.adjusting[s] ? "pumping" : "idle");
    num(skFav, bed.favorite[s], "");
    num(skHead, bed.position[s][ACT_HEAD], "%");
    num(skFoot, bed.position[s][ACT_FOOT], "%");
    setText(sideVal[skPreset][s], bed.preset[s][0] ? bed.preset[s] : "-");
  }

  static char logText[kLogSize * 96];
  logText[0] = 0;
  for (int n = 0; n < bed.logCount; n++) {
    const LogEntry& e = bed.log[(bed.logHead + kLogSize - 1 - n) % kLogSize];
    char line[110];
    snprintf(line, sizeof(line), "%s %s %s#  %s  #94A3B8 %ums#\n", e.ok ? "#34D399" : "#F05252",
             e.ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, e.cmd, e.resp, e.ms);
    strlcat(logText, line, sizeof(logText));
  }
  setText(logLabel, logText[0] ? logText : "No commands yet");
}

// ---- Names overlay ------------------------------------------------------------------

constexpr int kNameMax = sizeof(RemoteSettings::sideName[0]) - 1;

lv_obj_t* namesOverlay;
lv_obj_t* nameField[2];
lv_obj_t* keyboard;

void saveNames() {
  for (int s = 0; s < 2; s++) {
    char buf[kNameMax + 1];
    const char* t = lv_textarea_get_text(nameField[s]);
    while (*t == ' ') t++;
    strlcpy(buf, t, sizeof(buf));
    for (int n = strlen(buf); n > 0 && buf[n - 1] == ' '; n--) buf[n - 1] = 0;
    strlcpy(Settings::get().sideName[s], buf, sizeof(buf));  // empty = default name
  }
  Settings::save();
}

void onNamesDone(lv_event_t*) {
  saveNames();
  setHidden(namesOverlay, true);
}

void onNameFocused(lv_event_t* e) { lv_keyboard_set_textarea(keyboard, lv_event_get_target(e)); }

void buildNames(lv_obj_t* scr) {
  lv_obj_t* content;
  namesOverlay = overlay(scr, "Names", onNamesDone, &content);
  const char* hints[2] = {"Left side", "Right side"};
  for (int s = 0; s < 2; s++) {
    lv_obj_t* h = text(content, hints[s], &lv_font_montserrat_12, kMuted);
    lv_obj_set_pos(h, 12, s * 56);
    lv_obj_t* ta = lv_textarea_create(content);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, kNameMax);
    lv_obj_set_size(ta, kW - 12, 40);
    lv_obj_set_pos(ta, 6, s * 56 + 15);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ta, kText, 0);
    lv_obj_set_style_bg_color(ta, kSurface, 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_border_color(ta, kSurface, 0);
    lv_obj_set_style_border_color(ta, kAccent, LV_STATE_FOCUSED);
    lv_obj_set_style_pad_ver(ta, 9, 0);
    lv_obj_set_style_bg_color(ta, kAccent, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, onNameFocused, LV_EVENT_FOCUSED, nullptr);
    nameField[s] = ta;
  }

  // Letters only, 10 keys per row max, so keys stay finger-sized in portrait.
  static const char* lower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
                                "-", " ", LV_SYMBOL_OK, ""};
  static const char* upper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
                                "-", " ", LV_SYMBOL_OK, ""};
  static const lv_btnmatrix_ctrl_t ctrl[] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1,
      LV_BTNMATRIX_CTRL_CHECKED | 3, 2, 2, 2, 2, 2, 2, 2, LV_BTNMATRIX_CTRL_CHECKED | 3,
      LV_BTNMATRIX_CTRL_CHECKED | 1, 4, LV_BTNMATRIX_CTRL_CHECKED | 2};
  keyboard = lv_keyboard_create(namesOverlay);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, lower, ctrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, upper, ctrl);
  lv_obj_set_size(keyboard, kW, kH - 50 - 120);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(keyboard, kBg, 0);
  lv_obj_set_style_pad_all(keyboard, 3, 0);
  lv_obj_set_style_pad_gap(keyboard, 3, 0);
  lv_obj_set_style_bg_color(keyboard, kSurface2, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard, kSurface, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(keyboard, kAccent, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keyboard, kText, LV_PART_ITEMS);
  lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
  // The keyboard's check key saves; its close key also just closes (and saves).
  lv_obj_add_event_cb(keyboard, onNamesDone, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(keyboard, onNamesDone, LV_EVENT_CANCEL, nullptr);
}

}  // namespace

// ---- Settings page ---------------------------------------------------------------

void buildSettings(lv_obj_t* page) {
  makeScrollList(page);

  lv_obj_t* r = listRow(page, LV_SYMBOL_BLUETOOTH, "", onBedRow, 52);
  bedTitle = lv_obj_get_child(r, 2);
  lv_obj_align(bedTitle, LV_ALIGN_TOP_LEFT, 44, 7);
  bedSub = text(r, "", &lv_font_montserrat_12, kMuted);
  lv_obj_align(bedSub, LV_ALIGN_BOTTOM_LEFT, 44, -7);

  r = listRow(page, LV_SYMBOL_EDIT, "Names", onNamesRow, 52);
  lv_obj_align(lv_obj_get_child(r, 2), LV_ALIGN_TOP_LEFT, 44, 7);
  namesSub = text(r, "", &lv_font_montserrat_12, kMuted);
  lv_obj_align(namesSub, LV_ALIGN_BOTTOM_LEFT, 44, -7);

  listRow(page, LV_SYMBOL_PLUS, "Pair a bed", onPairRow);

  r = listRow(page, LV_SYMBOL_SHUFFLE, "Swap sides", nullptr);
  swapSwitch = lv_switch_create(r);
  styleSwitch(swapSwitch);
  lv_obj_align(swapSwitch, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_event_cb(swapSwitch, onSwap, LV_EVENT_VALUE_CHANGED, nullptr);

  r = listRow(page, LV_SYMBOL_LOOP, "Flip screen", nullptr);
  flipSwitch = lv_switch_create(r);
  styleSwitch(flipSwitch);
  lv_obj_align(flipSwitch, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_event_cb(flipSwitch, onFlip, LV_EVENT_VALUE_CHANGED, nullptr);

  r = listRow(page, LV_SYMBOL_IMAGE, "Brightness", nullptr, 76);
  lv_obj_align(lv_obj_get_child(r, 0), LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_align(lv_obj_get_child(r, 1), LV_ALIGN_TOP_LEFT, 44, 12);
  brightSlider = lv_slider_create(r);
  lv_obj_set_size(brightSlider, kW - 12 - 2 * 26, 8);
  lv_obj_align(brightSlider, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_slider_set_range(brightSlider, 20, 255);
  lv_obj_set_style_bg_color(brightSlider, kSurface2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightSlider, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightSlider, kText, LV_PART_KNOB);
  lv_obj_set_style_pad_all(brightSlider, 8, LV_PART_KNOB);
  lv_obj_set_ext_click_area(brightSlider, 16);
  lv_obj_add_event_cb(brightSlider, onBright, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(brightSlider, onBright, LV_EVENT_RELEASED, nullptr);

  r = listRow(page, LV_SYMBOL_EYE_CLOSE, "Screen off after", nullptr, 88);
  lv_obj_align(lv_obj_get_child(r, 0), LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_align(lv_obj_get_child(r, 1), LV_ALIGN_TOP_LEFT, 44, 12);
  const int offW = (kW - 12 - 16 - 3 * 6) / 4;
  for (int i = 0; i < 4; i++) {
    offBtn[i] = button(r, kOffLabels[i], &lv_font_montserrat_14, kSurface2);
    lv_obj_set_size(offBtn[i], offW, 38);
    lv_obj_set_pos(offBtn[i], 8 + i * (offW + 6), 88 - 8 - 38);
    lv_obj_set_style_bg_color(offBtn[i], kAccent, LV_STATE_CHECKED);
    lv_obj_add_event_cb(offBtn[i], onOff, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  listRow(page, LV_SYMBOL_LIST, "Bed info & data", onBedRow);

  r = listRow(page, LV_SYMBOL_TRASH, "Forget pairing", onForget);
  lv_obj_set_style_text_color(lv_obj_get_child(r, 1), kDanger, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(r, 2), kDanger, 0);

  lv_obj_t* v = text(page, "Bed Remote " FW_VERSION, &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_pad_left(v, 8, 0);
}

void refreshSettings() {
  setText(bedTitle, bed.bedName[0] ? bed.bedName : "No bed paired");
  char sub[48];
  if (bed.link == LinkState::Ready) snprintf(sub, sizeof(sub), "Connected  %d dBm", bed.rssi);
  else strlcpy(sub, bed.status, sizeof(sub));
  setText(bedSub, sub);

  char names[32];
  snprintf(names, sizeof(names), "%s  |  %s", sideName(SIDE_LEFT), sideName(SIDE_RIGHT));
  setText(namesSub, names);

  const RemoteSettings& cfg = Settings::get();
  setChecked(swapSwitch, cfg.swapSides);
  setChecked(flipSwitch, cfg.flipScreen);
  if (!lv_obj_has_state(brightSlider, LV_STATE_PRESSED)) lv_slider_set_value(brightSlider, cfg.brightness, LV_ANIM_OFF);
  for (int i = 0; i < 4; i++) setChecked(offBtn[i], cfg.screenOffSec == kOffOptions[i]);
}

// ---- Overlays ----------------------------------------------------------------------

void buildOverlays(lv_obj_t* scr) {
  buildPairing(scr);
  buildInfo(scr);
  buildNames(scr);
}

void refreshOverlays() {
  refreshPairing();
  refreshInfo();
}

void openPairing(bool scanFirst) {
  setHidden(pairOverlay, false);
  lv_obj_move_foreground(pairOverlay);
  scanListVersion = UINT32_MAX;
  pairSawNotReady = false;
  if (scanFirst) {
    showPairStep(PairStep::Scan);
    BedLink::startScan();
  } else {
    showPairStep(PairStep::Wait);
  }
  refreshPairing();
}

void openNames() {
  for (int s = 0; s < 2; s++) lv_textarea_set_text(nameField[s], sideName((BedSide)s));
  setHidden(namesOverlay, false);
  lv_obj_move_foreground(namesOverlay);
  lv_obj_add_state(nameField[0], LV_STATE_FOCUSED);
  lv_obj_clear_state(nameField[1], LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(keyboard, nameField[0]);
}

void openInfo() {
  setHidden(infoOverlay, false);
  lv_obj_move_foreground(infoOverlay);
  lv_obj_scroll_to_y(infoList, 0, LV_ANIM_OFF);
  refreshInfo();
}

}  // namespace ui
