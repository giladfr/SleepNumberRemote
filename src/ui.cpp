// App shell: header (side selector / title + link status), page area, bottom
// navigation, and the STOP bar that replaces the navigation while the base moves.

#include "ui.h"

#include <Arduino.h>
#include <stdarg.h>

#include "config.h"
#include "settings.h"
#include "ui_internal.h"

namespace ui {

const lv_color_t kBg = lv_color_hex(0x0D1220);
const lv_color_t kSurface = lv_color_hex(0x182033);
const lv_color_t kSurface2 = lv_color_hex(0x243049);
const lv_color_t kLine = lv_color_hex(0x2C3854);
const lv_color_t kAccent = lv_color_hex(0x5B8CFF);
const lv_color_t kOk = lv_color_hex(0x34D399);
const lv_color_t kWarn = lv_color_hex(0xFBBF24);
const lv_color_t kDanger = lv_color_hex(0xF05252);
const lv_color_t kText = lv_color_hex(0xF1F5F9);
const lv_color_t kMuted = lv_color_hex(0x94A3B8);

Target target = Target::Left;
BedState bed;

namespace {

struct NavItem {
  const char* icon;
  const char* label;
};
const NavItem kNav[kPageCount] = {
    {LV_SYMBOL_HOME, "Position"},
    {LV_SYMBOL_TINT, "Firmness"},
    {LV_SYMBOL_EDIT, "Adjust"},
    {LV_SYMBOL_SETTINGS, "Settings"},
};
const char* const kTitles[kPageCount] = {nullptr, "Firmness", nullptr, "Settings"};

constexpr int kChipW = 56;
constexpr int kSegW = (kW - kChipW - 4 - 4 - 4 - 4) / 3;  // 56

int currentPage = kPagePresets;
uint32_t shownVersion = UINT32_MAX;
uint32_t motionTapAt = 0;

lv_obj_t* pages[kPageCount];
lv_obj_t* segment;
lv_obj_t* segBtn[3];
lv_obj_t* segLabel[3];
lv_obj_t* title;
lv_obj_t* chip;
lv_obj_t* chipIcon;
lv_obj_t* chipText;
lv_obj_t* nav;
lv_obj_t* navPill[kPageCount];
lv_obj_t* navIcon[kPageCount];
lv_obj_t* navLabel[kPageCount];
lv_obj_t* stopBar;

void refreshAll(bool force);

void onSegment(lv_event_t* e) {
  target = (Target)(intptr_t)lv_event_get_user_data(e);
  refreshAll(true);
}

void onNav(lv_event_t* e) { goToPage((int)(intptr_t)lv_event_get_user_data(e)); }

void onChip(lv_event_t*) {
  if (bed.link == LinkState::NeedsPairing || bed.link == LinkState::NoBed) openPairing(bed.link == LinkState::NoBed);
  else goToPage(kPageSettings);
}

void onStop(lv_event_t*) { stopAll(); }

void buildHeader(lv_obj_t* scr) {
  lv_obj_t* hdr = box(scr);
  lv_obj_set_size(hdr, kW, kHeaderH);

  // Segmented side selector: [Left name | Both | Right name]
  segment = panel(hdr, kSurface, 20);
  lv_obj_set_size(segment, 3 * kSegW + 4, 40);
  lv_obj_set_pos(segment, 4, 4);
  const char* labels[3] = {sideName(SIDE_LEFT), "Both", sideName(SIDE_RIGHT)};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* b = lv_btn_create(segment);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, kSegW, 36);
    lv_obj_set_pos(b, 2 + i * kSegW, 2);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, kAccent, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, kSurface2, LV_STATE_PRESSED);
    lv_obj_t* l = text(b, labels[i], &lv_font_montserrat_16, kText);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(l, kSegW - 6, 0);
    lv_obj_center(l);
    segLabel[i] = l;
    lv_obj_add_event_cb(b, onSegment, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    segBtn[i] = b;
  }

  title = text(hdr, "", &lv_font_montserrat_20, kText);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);

  // Link status chip (tap: settings, or pairing when needed)
  chip = panel(hdr, kSurface, 20);
  lv_obj_set_size(chip, kChipW, 40);
  lv_obj_set_pos(chip, kW - kChipW - 4, 4);
  lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(chip, kSurface2, LV_STATE_PRESSED);
  lv_obj_add_event_cb(chip, onChip, LV_EVENT_CLICKED, nullptr);
  chipIcon = text(chip, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_16, kMuted);
  lv_obj_align(chipIcon, LV_ALIGN_TOP_MID, 0, 3);
  chipText = text(chip, "", &lv_font_montserrat_12, kMuted);
  lv_obj_align(chipText, LV_ALIGN_BOTTOM_MID, 0, -3);
}

void buildNav(lv_obj_t* scr) {
  nav = panel(scr, kSurface, 0);
  lv_obj_set_size(nav, kW, kNavH);
  lv_obj_set_pos(nav, 0, kH - kNavH);
  for (int i = 0; i < kPageCount; i++) {
    lv_obj_t* item = box(nav);
    lv_obj_set_size(item, kW / kPageCount, kNavH);
    lv_obj_set_pos(item, i * kW / kPageCount, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(item, onNav, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    navPill[i] = panel(item, kSurface, 14);
    lv_obj_set_size(navPill[i], 48, 28);
    lv_obj_align(navPill[i], LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_clear_flag(navPill[i], LV_OBJ_FLAG_CLICKABLE);
    navIcon[i] = text(navPill[i], kNav[i].icon, &lv_font_montserrat_16, kMuted);
    lv_obj_center(navIcon[i]);
    navLabel[i] = text(item, kNav[i].label, &lv_font_montserrat_12, kMuted);
    lv_obj_align(navLabel[i], LV_ALIGN_BOTTOM_MID, 0, -5);
  }

  stopBar = lv_btn_create(scr);
  lv_obj_remove_style_all(stopBar);
  lv_obj_set_size(stopBar, kW - 12, kNavH - 6);
  lv_obj_set_pos(stopBar, 6, kH - kNavH + 2);
  lv_obj_set_style_radius(stopBar, 14, 0);
  lv_obj_set_style_bg_opa(stopBar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(stopBar, kDanger, 0);
  lv_obj_set_style_bg_color(stopBar, lv_color_darken(kDanger, 60), LV_STATE_PRESSED);
  lv_obj_t* l = text(stopBar, LV_SYMBOL_STOP "   STOP", &lv_font_montserrat_20, kText);
  lv_obj_center(l);
  lv_obj_add_event_cb(stopBar, onStop, LV_EVENT_PRESSED, nullptr);
  setHidden(stopBar, true);
}

void refreshShell() {
  // Header: side selector on control pages, a title elsewhere
  bool sidePage = kTitles[currentPage] == nullptr;
  setHidden(segment, !sidePage);
  setHidden(title, sidePage);
  if (!sidePage) setText(title, kTitles[currentPage]);
  for (int i = 0; i < 3; i++) setChecked(segBtn[i], (int)target == i);
  for (int s = 0; s < 2; s++) setText(segLabel[s * 2], sideName((BedSide)s));

  lv_color_t c = kMuted;
  const char* t = bed.status;
  char buf[16];
  switch (bed.link) {
    case LinkState::Ready:
      c = kOk;
      snprintf(buf, sizeof(buf), "%d dBm", bed.rssi);
      t = buf;
      break;
    case LinkState::Connecting: c = kWarn; t = "Linking"; break;
    case LinkState::Scanning: c = kWarn; t = "Scanning"; break;
    case LinkState::NeedsPairing: case LinkState::NoBed: c = kDanger; t = "Pair"; break;
    case LinkState::Busy: c = kDanger; t = "Busy"; break;
    case LinkState::Asleep: t = "Asleep"; break;
    case LinkState::Error: c = kDanger; t = "Offline"; break;
  }
  setTextColor(chipIcon, c);
  setText(chipText, t);

  for (int i = 0; i < kPageCount; i++) {
    bool on = i == currentPage;
    setBgColor(navPill[i], on ? kSurface2 : kSurface);
    setTextColor(navIcon[i], on ? kAccent : kMuted);
    setTextColor(navLabel[i], on ? kText : kMuted);
  }

  bool showStop = bed.moving || (motionTapAt && millis() - motionTapAt < 2500);
  setHidden(stopBar, !showStop);
}

void refreshAll(bool force) {
  BedState now = BedLink::snapshot();
  if (!force && now.version == shownVersion) return;
  bed = now;
  shownVersion = bed.version;
  refreshShell();
  switch (currentPage) {
    case kPagePresets: refreshPresets(); break;
    case kPageFirmness: refreshFirmness(); break;
    case kPageAdjust: refreshAdjust(); break;
    case kPageSettings: refreshSettings(); break;
  }
  refreshOverlays();
}

void tick(lv_timer_t*) {
  tickFirmness();
  // STOP bar timeout and other time-based bits need a periodic forced refresh.
  static uint32_t lastForced = 0;
  bool force = millis() - lastForced > 500;
  if (force) lastForced = millis();
  refreshAll(force);
}

}  // namespace

// ---- Helpers -----------------------------------------------------------------

const char* sideName(BedSide s) {
  const char* n = Settings::get().sideName[s];
  return n[0] ? n : (s == SIDE_LEFT ? LEFT_NAME : RIGHT_NAME);
}

void setText(lv_obj_t* label, const char* s) {
  if (strcmp(lv_label_get_text(label), s) != 0) lv_label_set_text(label, s);
}

void setTextFmt(lv_obj_t* label, const char* fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  setText(label, buf);
}

void setTextColor(lv_obj_t* o, lv_color_t c) {
  if (lv_obj_get_style_text_color(o, LV_PART_MAIN).full != c.full) lv_obj_set_style_text_color(o, c, 0);
}

void setBgColor(lv_obj_t* o, lv_color_t c) {
  if (lv_obj_get_style_bg_color(o, LV_PART_MAIN).full != c.full) lv_obj_set_style_bg_color(o, c, 0);
}

void setLineColor(lv_obj_t* o, lv_color_t c) {
  if (lv_obj_get_style_line_color(o, LV_PART_MAIN).full != c.full) lv_obj_set_style_line_color(o, c, 0);
}

void setWidth(lv_obj_t* o, lv_coord_t w) {
  if (lv_obj_get_style_width(o, LV_PART_MAIN) != w) lv_obj_set_width(o, w);
}

void setX(lv_obj_t* o, lv_coord_t x) {
  if (lv_obj_get_style_x(o, LV_PART_MAIN) != x) lv_obj_set_x(o, x);
}

lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* panel(lv_obj_t* parent, lv_color_t bg, int radius) {
  lv_obj_t* o = box(parent);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

lv_obj_t* button(lv_obj_t* parent, const char* label, const lv_font_t* font, lv_color_t bg) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_color(b, lv_color_lighten(bg, 30), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_t* l = text(b, label, font, kText);
  lv_obj_center(l);
  return b;
}

void setHidden(lv_obj_t* o, bool hidden) {
  if (hidden == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void setChecked(lv_obj_t* o, bool checked) {
  if (checked) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_clear_state(o, LV_STATE_CHECKED);
}

void sendCmd(CmdType type, BedSide side, int value, Actuator act, const char* preset) {
  BedCmd c;
  c.type = type;
  c.side = side;
  c.value = value;
  c.actuator = act;
  if (preset) strlcpy(c.preset, preset, sizeof(c.preset));
  BedLink::send(c);
}

void noteMotionCommand() {
  motionTapAt = millis();
  refreshAll(true);
}

void stopAll() {
  sendCmd(CmdType::HaltAll, SIDE_LEFT);
  motionTapAt = 0;
}

// Only the visible page exists: the others are deleted and rebuilt on the next
// visit, which leaves RAM for Wi-Fi and HomeKit.
void goToPage(int page) {
  static int built = -1;
  if (page != built) {
    if (built >= 0) lv_obj_clean(pages[built]);
    switch (page) {
      case kPagePresets: buildPresets(pages[page]); break;
      case kPageFirmness: buildFirmness(pages[page]); break;
      case kPageAdjust: buildAdjust(pages[page]); break;
      case kPageSettings: buildSettings(pages[page]); break;
    }
    built = page;
  }
  currentPage = page;
  for (int i = 0; i < kPageCount; i++) setHidden(pages[i], i != page);
  refreshAll(true);
}

}  // namespace ui

namespace Ui {

void build() {
  using namespace ui;
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, kBg, 0);
  lv_obj_set_style_text_color(scr, kText, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildHeader(scr);
  for (int i = 0; i < kPageCount; i++) {
    pages[i] = box(scr);
    lv_obj_set_size(pages[i], kW, kBodyH);
    lv_obj_set_pos(pages[i], 0, kHeaderH);
  }
  buildNav(scr);
  buildOverlays(scr);

  goToPage(kPagePresets);
  lv_timer_create(tick, 120, nullptr);
}

}  // namespace Ui
