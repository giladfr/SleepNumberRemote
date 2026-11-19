// HomeKit screens: status + setup code (QR), Wi-Fi network picker, password entry
// and the "Restarting" screen shown when a HomeKit change needs a restart.

#include <Arduino.h>

#include "homekit.h"
#include "net_api.h"
#include "settings.h"
#include "ui.h"
#include "ui_internal.h"

namespace ui {

namespace {

constexpr int kContentH = kH - 50;
constexpr int kQrSize = 124;

HkStatus hk;

// Text from the radio, shown on screen: printable ASCII only (the fonts have nothing else).
void displayName(const char* in, char* out, size_t n) {
  strlcpy(out, in, n);
  for (char* c = out; *c; c++)
    if (*c < 0x20 || *c > 0x7E) *c = '?';
}

// ---- HomeKit overlay ------------------------------------------------------------

lv_obj_t* hkOverlay = nullptr;
lv_obj_t* hkList;
lv_obj_t* hkIconOk;
lv_obj_t* hkIconHome;
lv_obj_t* hkSpinner;
lv_obj_t* hkQrWrap;
lv_obj_t* hkQr;
lv_obj_t* hkCode;
lv_obj_t* hkHeadline;
lv_obj_t* hkBody;
lv_obj_t* hkSwitch;
lv_obj_t* wifiRow;
lv_obj_t* wifiSub;
lv_obj_t* apiRow;
lv_obj_t* apiSwitch;
lv_obj_t* apiSub;
lv_obj_t* resetRow;
lv_obj_t* hkFooter;
char qrShown[24] = "";

lv_obj_t* restartView = nullptr;

void openWifi();

void onHkBack(lv_event_t*) { closeOverlay(hkOverlay); }

void onHkSwitch(lv_event_t*) {
  bool on = lv_obj_has_state(hkSwitch, LV_STATE_CHECKED);
  HomeKit::setEnabled(on);
  if (on && !Settings::get().wifiSsid[0]) openWifi();
  refreshHomeKit();
}

void onWifiRow(lv_event_t*) { openWifi(); }

void onApiSwitch(lv_event_t*) {
  RemoteSettings& cfg = Settings::get();
  cfg.api = lv_obj_has_state(apiSwitch, LV_STATE_CHECKED);
  Settings::save();
  NetApi::setEnabled(cfg.api);
  refreshHomeKit();
}

void onResetConfirm(lv_event_t* e) {
  lv_obj_t* mbox = lv_event_get_current_target(e);
  const char* btn = lv_msgbox_get_active_btn_text(mbox);
  if (btn && strcmp(btn, "Reset") == 0) HomeKit::resetPairing();
  lv_msgbox_close_async(mbox);
}

void onReset(lv_event_t*) {
  static const char* btns[] = {"Cancel", "Reset", ""};
  confirmBox("Reset HomeKit?",
             "Unpairs from Apple Home and makes a new setup code. Remove \"Bed Remote\" in the Home app too.", btns,
             onResetConfirm);
}

lv_obj_t* wrapText(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = text(parent, "", font, color);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

void buildHkOverlay(lv_obj_t* scr) {
  qrShown[0] = 0;
  hkOverlay = overlay(scr, "HomeKit", onHkBack, &hkList);
  makeScrollList(hkList);

  // Status card: what to do next, or the setup code while waiting to be added.
  lv_obj_t* card = panel(hkList, kSurface, 14);
  lv_obj_set_size(card, kW - 12, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  hkIconHome = panel(card, kSurface2, LV_RADIUS_CIRCLE);
  lv_obj_set_size(hkIconHome, 52, 52);
  lv_obj_center(text(hkIconHome, LV_SYMBOL_HOME, &lv_font_montserrat_20, kAccent));
  hkIconOk = panel(card, kOk, LV_RADIUS_CIRCLE);
  lv_obj_set_size(hkIconOk, 52, 52);
  lv_obj_center(text(hkIconOk, LV_SYMBOL_OK, &lv_font_montserrat_28, kBg));
  hkSpinner = spinner(card, 52, 5);

  hkQrWrap = panel(card, lv_color_white(), 12);  // QR codes need a light quiet zone
  lv_obj_set_size(hkQrWrap, kQrSize + 16, kQrSize + 16);
  hkQr = lv_qrcode_create(hkQrWrap, kQrSize, lv_color_black(), lv_color_white());
  lv_obj_center(hkQr);
  hkCode = text(card, "", &lv_font_montserrat_28, kText);

  hkHeadline = wrapText(card, &lv_font_montserrat_16, kText);
  hkBody = wrapText(card, &lv_font_montserrat_14, kMuted);

  lv_obj_t* r = listRow(hkList, LV_SYMBOL_HOME, "Apple Home", nullptr);
  hkSwitch = lv_switch_create(r);
  styleSwitch(hkSwitch);
  lv_obj_align(hkSwitch, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_event_cb(hkSwitch, onHkSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

  wifiRow = listRow(hkList, LV_SYMBOL_WIFI, "Wi-Fi", onWifiRow, 52);
  lv_obj_align(lv_obj_get_child(wifiRow, 2), LV_ALIGN_TOP_LEFT, 44, 7);
  wifiSub = text(wifiRow, "", &lv_font_montserrat_12, kMuted);
  lv_obj_align(wifiSub, LV_ALIGN_BOTTOM_LEFT, 44, -7);

  // Local HTTP API (net_api.h): shares the Wi-Fi connection.
  apiRow = listRow(hkList, LV_SYMBOL_SHUFFLE, "Local API", nullptr, 52);
  lv_obj_align(lv_obj_get_child(apiRow, 1), LV_ALIGN_TOP_LEFT, 44, 7);
  apiSub = text(apiRow, "", &lv_font_montserrat_12, kMuted);
  lv_obj_align(apiSub, LV_ALIGN_BOTTOM_LEFT, 44, -7);
  apiSwitch = lv_switch_create(apiRow);
  styleSwitch(apiSwitch);
  lv_obj_align(apiSwitch, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_event_cb(apiSwitch, onApiSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

  resetRow = listRow(hkList, LV_SYMBOL_TRASH, "Reset HomeKit", onReset);
  lv_obj_set_style_text_color(lv_obj_get_child(resetRow, 1), kDanger, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(resetRow, 2), kDanger, 0);

  hkFooter = text(hkList, "", &lv_font_montserrat_12, kMuted);
  lv_label_set_long_mode(hkFooter, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hkFooter, kW - 28);
  lv_obj_set_style_pad_left(hkFooter, 8, 0);
}

enum class Card { Off, NeedWifi, Connecting, Pair, Paired };

void showCard(Card c) {
  setHidden(hkIconHome, c != Card::Off && c != Card::NeedWifi);
  setHidden(hkIconOk, c != Card::Paired);
  setHidden(hkSpinner, c != Card::Connecting);
  setHidden(hkQrWrap, c != Card::Pair);
  setHidden(hkCode, c != Card::Pair);
}

void refreshHkOverlay() {
  const RemoteSettings& cfg = Settings::get();
  char b[128];
  setChecked(hkSwitch, cfg.homekit);
  setHidden(wifiRow, !cfg.homekit);
  setHidden(resetRow, !cfg.homekit || !cfg.hkCode[0]);
  setHidden(apiRow, !cfg.homekit);
  setChecked(apiSwitch, cfg.api);
  if (!cfg.api) setText(apiSub, "Off");
  else setTextFmt(apiSub, "On, port %u", (unsigned)NetApi::kPort);

  if (!cfg.homekit) {
    showCard(Card::Off);
    setText(hkHeadline, "Use the Home app and Siri");
    setText(hkBody, "Control both sides from your iPhone. The remote joins your Wi-Fi; nothing goes through the cloud.");
  } else if (!cfg.wifiSsid[0]) {
    showCard(Card::NeedWifi);
    setText(hkHeadline, "Choose a Wi-Fi network");
    setText(hkBody, "HomeKit works over your home Wi-Fi.");
  } else if (hk.state == HkState::NeedsPairing) {
    showCard(Card::Pair);
    if (strcmp(qrShown, hk.payload) != 0) {
      strlcpy(qrShown, hk.payload, sizeof(qrShown));
      lv_qrcode_update(hkQr, qrShown, strlen(qrShown));
    }
    setText(hkCode, hk.code);
    setText(hkHeadline, "Add to Apple Home");
    setText(hkBody, "In the Home app tap + and Add Accessory, then scan the code or type the number.");
  } else if (hk.state == HkState::Paired) {
    showCard(Card::Paired);
    setText(hkHeadline, "Added to Apple Home");
    snprintf(b, sizeof(b), "Presets for %s, %s and both sides. Try \"Hey Siri, turn on Zero G\" or \"turn on %s Snore\".",
             sideName(SIDE_LEFT), sideName(SIDE_RIGHT), sideName(SIDE_LEFT));
    setText(hkBody, b);
  } else {
    showCard(Card::Connecting);
    displayName(cfg.wifiSsid, b, sizeof(b));
    setTextFmt(hkHeadline, "Connecting to %s", b);
    setText(hkBody, hk.state == HkState::Starting ? "Starting HomeKit..." : "If this takes long, check the Wi-Fi password.");
  }

  displayName(cfg.wifiSsid[0] ? cfg.wifiSsid : "Not set", b, sizeof(b));
  setText(wifiSub, b);
  if (hk.ip[0] && cfg.api)
    snprintf(b, sizeof(b), "API http://%s.local:%u\nIP %s   Free memory %u KB (low %u KB)", NetApi::kHostName,
             (unsigned)NetApi::kPort, hk.ip, (unsigned)(hk.freeHeap / 1024), (unsigned)(hk.minHeap / 1024));
  else if (hk.ip[0]) snprintf(b, sizeof(b), "IP %s   Free memory %u KB (low %u KB)", hk.ip, (unsigned)(hk.freeHeap / 1024),
                              (unsigned)(hk.minHeap / 1024));
  else snprintf(b, sizeof(b), "Free memory %u KB (low %u KB)", (unsigned)(hk.freeHeap / 1024), (unsigned)(hk.minHeap / 1024));
  setText(hkFooter, b);
}

void buildRestart(lv_obj_t* scr) {
  restartView = panel(scr, kBg, 0);
  lv_obj_set_size(restartView, kW, kH);
  lv_obj_t* sp = spinner(restartView, 64, 6);
  lv_obj_align(sp, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t* t = text(restartView, "Restarting", &lv_font_montserrat_20, kText);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 30);
  lv_obj_t* s = text(restartView, "Applying HomeKit settings", &lv_font_montserrat_14, kMuted);
  lv_obj_align(s, LV_ALIGN_CENTER, 0, 58);
  setHidden(restartView, true);
}

// ---- Wi-Fi picker -----------------------------------------------------------------

constexpr int kMaxNets = 10;

lv_obj_t* wifiOverlay = nullptr;
lv_obj_t* wifiStatus;
lv_obj_t* wifiList;
lv_obj_t* wifiSpinner;
lv_obj_t* wifiAgain;
WifiNet nets[kMaxNets];
int netCount = -1;  // -1 = scanning, -2 = failed
int netsShown = -2;

void openPassword(const WifiNet& net);

void join(const char* ssid, const char* pass) {
  HomeKit::setWifi(ssid, pass);  // first, so HomeKit starts with the new network
  HomeKit::setEnabled(true);
}

void onWifiBack(lv_event_t*) { closeOverlay(wifiOverlay); }

void onWifiAgain(lv_event_t*) {
  HomeKit::startWifiScan();
  netCount = -1;
  refreshHomeKit();
}

void onNet(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= netCount) return;
  if (nets[i].secure) {
    openPassword(nets[i]);
  } else {
    join(nets[i].ssid, "");
    closeOverlay(wifiOverlay);
  }
}

void rebuildNetList() {
  lv_obj_clean(wifiList);
  for (int i = 0; i < netCount; i++) {
    char name[40];
    displayName(nets[i].ssid, name, sizeof(name));
    lv_obj_t* r = listRow(wifiList, LV_SYMBOL_WIFI, name, nullptr, 52);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, kSurface2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, onNet, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* label = lv_obj_get_child(r, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, kW - 12 - 44 - 14);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 44, 7);
    int q = nets[i].rssi;
    const char* strength = q > -60 ? "strong" : q > -72 ? "good" : "weak";
    lv_obj_t* sub = text(r, "", &lv_font_montserrat_12, kMuted);
    lv_label_set_text_fmt(sub, "%s  -  %s signal", nets[i].secure ? "Secured" : "Open", strength);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 44, -7);
    if (strcmp(nets[i].ssid, Settings::get().wifiSsid) == 0) lv_obj_set_style_text_color(label, kOk, 0);
  }
}

void buildWifi(lv_obj_t* scr) {
  lv_obj_t* content;
  wifiOverlay = overlay(scr, "Wi-Fi", onWifiBack, &content);
  wifiStatus = text(content, "", &lv_font_montserrat_14, kMuted);
  lv_label_set_long_mode(wifiStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(wifiStatus, kW - 28);
  lv_obj_set_pos(wifiStatus, 14, 0);
  wifiList = box(content);
  lv_obj_set_size(wifiList, kW, kContentH - 26 - 56);
  lv_obj_set_pos(wifiList, 0, 24);
  makeScrollList(wifiList);
  wifiSpinner = spinner(content, 48, 5);
  lv_obj_align(wifiSpinner, LV_ALIGN_TOP_MID, 0, 70);
  wifiAgain = button(content, LV_SYMBOL_REFRESH "  Scan again", &lv_font_montserrat_16, kSurface2);
  lv_obj_set_size(wifiAgain, kW - 12, 44);
  lv_obj_set_pos(wifiAgain, 6, kContentH - 52);
  lv_obj_add_event_cb(wifiAgain, onWifiAgain, LV_EVENT_CLICKED, nullptr);
}

void refreshWifi() {
  if (netCount == -1) netCount = HomeKit::wifiScan(nets, kMaxNets);
  if (netCount != netsShown) {
    netsShown = netCount;
    rebuildNetList();
  }
  bool scanning = netCount == -1;
  if (scanning) setText(wifiStatus, "Searching for networks...");
  else if (netCount < 0) setText(wifiStatus, "Wi-Fi didn't start. Scan again, or restart the remote.");
  else if (netCount == 0) setText(wifiStatus, "No networks found. HomeKit needs 2.4 GHz Wi-Fi.");
  else setText(wifiStatus, "Choose your home network (2.4 GHz):");
  setHidden(wifiSpinner, !scanning);
  if (scanning) lv_obj_add_state(wifiAgain, LV_STATE_DISABLED);
  else lv_obj_clear_state(wifiAgain, LV_STATE_DISABLED);
}

void openWifi() {
  if (!wifiOverlay) {
    buildWifi(lv_scr_act());
    netsShown = -2;
  }
  setHidden(wifiOverlay, false);
  lv_obj_move_foreground(wifiOverlay);
  onWifiAgain(nullptr);
}

// ---- Password entry ---------------------------------------------------------------

lv_obj_t* passOverlay = nullptr;
lv_obj_t* passFor;
lv_obj_t* passField;
lv_obj_t* passEye;
lv_obj_t* passKeyboard;
char passSsid[33];

void onPassBack(lv_event_t*) { closeOverlay(passOverlay); }

void onPassJoin(lv_event_t*) {
  join(passSsid, lv_textarea_get_text(passField));
  lv_textarea_set_text(passField, "");  // don't keep it in the widget
  closeOverlay(passOverlay);
  closeOverlay(wifiOverlay);
  refreshHomeKit();
}

void onEye(lv_event_t*) {
  bool hidden = lv_textarea_get_password_mode(passField);
  lv_textarea_set_password_mode(passField, !hidden);
  lv_label_set_text(lv_obj_get_child(passEye, 0), hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void buildPassword(lv_obj_t* scr) {
  lv_obj_t* content;
  passOverlay = overlay(scr, "Password", onPassBack, &content);
  passFor = text(content, "", &lv_font_montserrat_12, kMuted);
  lv_label_set_long_mode(passFor, LV_LABEL_LONG_DOT);
  lv_obj_set_width(passFor, kW - 24);
  lv_obj_set_pos(passFor, 12, 0);

  passField = textField(content);
  lv_textarea_set_max_length(passField, sizeof(RemoteSettings::wifiPass) - 1);
  lv_textarea_set_password_mode(passField, true);
  lv_textarea_set_password_show_time(passField, 800);
  lv_obj_set_size(passField, kW - 12 - 52, 40);
  lv_obj_set_pos(passField, 6, 16);
  lv_obj_add_state(passField, LV_STATE_FOCUSED);

  passEye = button(content, LV_SYMBOL_EYE_OPEN, &lv_font_montserrat_16, kSurface);
  lv_obj_set_size(passEye, 46, 40);
  lv_obj_set_pos(passEye, kW - 6 - 46, 16);
  lv_obj_add_event_cb(passEye, onEye, LV_EVENT_CLICKED, nullptr);

  // Full keyboard, at most 10 keys per row so they stay finger-sized.
  static const char* lower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
                                "1#", " ", LV_SYMBOL_OK, ""};
  static const char* upper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
                                "1#", " ", LV_SYMBOL_OK, ""};
  static const lv_btnmatrix_ctrl_t letterCtrl[] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1,
      LV_BTNMATRIX_CTRL_CHECKED | 3, 2, 2, 2, 2, 2, 2, 2, LV_BTNMATRIX_CTRL_CHECKED | 3,
      LV_BTNMATRIX_CTRL_CHECKED | 2, 5, LV_BTNMATRIX_CTRL_CHECKED | 2};
  static const char* symbols[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                  "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", "\n",
                                  ".", ",", "?", "!", "'", "_", "#", "%", "+", "=", "\n",
                                  "[", "]", "{", "}", "<", ">", "*", "^", "~", "\\", "\n",
                                  "abc", "|", "`", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};
  static const lv_btnmatrix_ctrl_t symbolCtrl[] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      LV_BTNMATRIX_CTRL_CHECKED | 3, 2, 2, 4, LV_BTNMATRIX_CTRL_CHECKED | 3, LV_BTNMATRIX_CTRL_CHECKED | 3};
  passKeyboard = lv_keyboard_create(passOverlay);
  lv_keyboard_set_map(passKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER, lower, letterCtrl);
  lv_keyboard_set_map(passKeyboard, LV_KEYBOARD_MODE_TEXT_UPPER, upper, letterCtrl);
  lv_keyboard_set_map(passKeyboard, LV_KEYBOARD_MODE_SPECIAL, symbols, symbolCtrl);
  lv_obj_set_size(passKeyboard, kW, kContentH - 64);
  lv_obj_align(passKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  styleKeyboard(passKeyboard);
  lv_keyboard_set_textarea(passKeyboard, passField);
  lv_obj_add_event_cb(passKeyboard, onPassJoin, LV_EVENT_READY, nullptr);
}

void openPassword(const WifiNet& net) {
  if (!passOverlay) buildPassword(lv_scr_act());
  strlcpy(passSsid, net.ssid, sizeof(passSsid));
  char name[40];
  displayName(net.ssid, name, sizeof(name));
  lv_label_set_text_fmt(passFor, "Network: %s", name);
  lv_textarea_set_text(passField, "");
  lv_textarea_set_password_mode(passField, true);
  lv_label_set_text(lv_obj_get_child(passEye, 0), LV_SYMBOL_EYE_OPEN);
  lv_keyboard_set_mode(passKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  setHidden(passOverlay, false);
  lv_obj_move_foreground(passOverlay);
}

}  // namespace

void buildHomeKit(lv_obj_t*) {}  // screens are built when opened (see buildOverlays)

void refreshHomeKit() {
  hk = HomeKit::status();
  if (HomeKit::pendingRestart() == HomeKit::Restart::Now && !restartView) {
    buildRestart(lv_scr_act());
    setHidden(restartView, false);
    lv_obj_move_foreground(restartView);
  }
  if (hkOverlay) refreshHkOverlay();
  if (wifiOverlay) refreshWifi();
}

const char* homeKitSummary() {
  const RemoteSettings& cfg = Settings::get();
  if (!cfg.homekit) return "Off";
  if (!cfg.wifiSsid[0]) return "Choose a Wi-Fi network";
  switch (hk.state) {
    case HkState::Paired: return "On - added to Apple Home";
    case HkState::NeedsPairing: return "Ready to add to Apple Home";
    default: return "Connecting to Wi-Fi";
  }
}

void openHomeKit() {
  if (!hkOverlay) buildHkOverlay(lv_scr_act());
  setHidden(hkOverlay, false);
  lv_obj_move_foreground(hkOverlay);
  lv_obj_scroll_to_y(hkList, 0, LV_ANIM_OFF);
  refreshHomeKit();
}

}  // namespace ui

namespace Ui {
void openHomeKit() { ui::openHomeKit(); }
}  // namespace Ui
