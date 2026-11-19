// Control pages: base positions (home), firmness for both sleepers, and fine
// head/foot adjustment.

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "ui_internal.h"

namespace ui {

namespace {

// ---- Positions ---------------------------------------------------------------

struct PresetDef {
  const char* wire;
  const char* label;
  int headDeg;  // pose for the little bed icon
  int knee;
  int foot;
};
const PresetDef kPresets[] = {
    {"flat", "Flat", 0, 0, 0},       {"zero_g", "Zero G", 32, 10, 7}, {"snore", "Snore", 22, 0, 0},
    {"favorite", "Favorite", 24, 5, 3}, {"watch_tv", "TV", 45, 4, 2},   {"read", "Read", 60, 2, 0},
};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

lv_obj_t* tiles[kPresetCount];
lv_obj_t* tileLine[kPresetCount];
lv_obj_t* tileHead[kPresetCount];
lv_obj_t* tileIcon[kPresetCount];
lv_obj_t* tileLabel[kPresetCount];
int tileRows = 0;
lv_point_t posePts[kPresetCount][4];

char pendingPreset[2][16];
uint32_t pendingPresetAt[2];

bool presetAvailable(int i) {
  const bool avail[kPresetCount] = {bed.presetFlat, bed.presetZeroG, bed.presetSnore,
                                    bed.presetFavorite, bed.presetTv, bed.presetRead};
  return avail[i];
}

constexpr int kIconW = 84, kIconH = 40;
constexpr float kIconScale = 1.3f;

// Mattress outline: head section pivots at the hip, legs bend at the knee.
void drawPose(lv_obj_t* parent, int i) {
  const PresetDef& p = kPresets[i];
  const float k = kIconScale;
  float a = p.headDeg * (float)M_PI / 180.f;
  const float hipX = 24 * k, baseY = 28 * k, headLen = 21 * k;
  int hx = (int)lroundf(hipX - headLen * cosf(a)), hy = (int)lroundf(baseY - headLen * sinf(a));
  lv_point_t* pts = posePts[i];
  pts[0] = {(lv_coord_t)hx, (lv_coord_t)hy};
  pts[1] = {(lv_coord_t)lroundf(hipX), (lv_coord_t)lroundf(baseY)};
  pts[2] = {(lv_coord_t)lroundf(44 * k), (lv_coord_t)lroundf(baseY - p.knee * k)};
  pts[3] = {(lv_coord_t)lroundf(60 * k), (lv_coord_t)lroundf(baseY - p.foot * k)};

  tileLine[i] = lv_line_create(parent);
  lv_line_set_points(tileLine[i], pts, 4);
  lv_obj_set_style_line_width(tileLine[i], 6, 0);
  lv_obj_set_style_line_rounded(tileLine[i], true, 0);

  // Sleeper's head resting just above the head end of the mattress.
  tileHead[i] = panel(parent, kText, LV_RADIUS_CIRCLE);
  lv_obj_set_size(tileHead[i], 12, 12);
  float cx = hx + 7 * k * cosf(a) + 8 * k * sinf(a), cy = hy + 7 * k * sinf(a) - 8 * k * cosf(a);
  lv_obj_set_pos(tileHead[i], (lv_coord_t)lroundf(cx) - 6, (lv_coord_t)lroundf(cy) - 6);
}

void onPreset(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  for (int s = 0; s < 2; s++) {
    if (!targets((BedSide)s)) continue;
    sendCmd(CmdType::Preset, (BedSide)s, 0, ACT_HEAD, kPresets[i].wire);
    strlcpy(pendingPreset[s], kPresets[i].wire, sizeof(pendingPreset[s]));
    pendingPresetAt[s] = millis();
  }
  noteMotionCommand();
}

const char* shownPreset(int s) {
  if (pendingPreset[s][0]) {
    if (strcmp(bed.preset[s], pendingPreset[s]) == 0 || millis() - pendingPresetAt[s] > 30000) pendingPreset[s][0] = 0;
    else return pendingPreset[s];
  }
  return bed.preset[s];
}

// ---- Firmness ----------------------------------------------------------------

struct PendingSn {
  int value = -1;
  uint32_t changedAt = 0;
  bool sent = false;
  uint32_t sentAt = 0;
} pending[2];

lv_obj_t* snName[2];
lv_obj_t* snValue[2];
lv_obj_t* snHint[2];
lv_obj_t* snState[2];

int displayedSn(int s) { return pending[s].value >= 0 ? pending[s].value : bed.sleepNumber[s]; }

void setPendingSn(int s, int value, bool immediate) {
  value = LV_CLAMP(5, (value + 2) / 5 * 5, 100);
  pending[s].value = value;
  pending[s].sent = false;
  pending[s].changedAt = immediate ? millis() - SN_APPLY_DELAY_MS : millis();
  refreshFirmness();
}

void onSnStep(lv_event_t* e) {
  int code = (int)(intptr_t)lv_event_get_user_data(e);  // side * 2 + (0 = minus, 1 = plus)
  int s = code / 2;
  int cur = displayedSn(s);
  if (cur >= 0) setPendingSn(s, cur + (code % 2 ? 5 : -5), false);
}

void onSnNumber(lv_event_t* e) {
  int s = (int)(intptr_t)lv_event_get_user_data(e);
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
    int cur = displayedSn(s);
    if (cur > 0) sendCmd(CmdType::SetFavorite, (BedSide)s, cur);
  } else if (bed.favorite[s] > 0) {
    setPendingSn(s, bed.favorite[s], true);
  }
}

// ---- Adjust ------------------------------------------------------------------

lv_obj_t* slider[2];
lv_obj_t* sliderValue[2];
lv_obj_t* lightBtn;
lv_obj_t* adjustStop;
uint32_t sliderHoldUntil[2];
uint32_t lightHoldUntil = 0;

void sendActuator(int act, int value) {
  value = LV_CLAMP(0, value, 100);
  for (int s = 0; s < 2; s++)
    if (targets((BedSide)s) && bed.hasActuator[s][act]) sendCmd(CmdType::SetActuator, (BedSide)s, value, (Actuator)act);
  sliderHoldUntil[act] = millis() + 4000;
  lv_slider_set_value(slider[act], value, LV_ANIM_ON);
  lv_label_set_text_fmt(sliderValue[act], "%d%%", value);
  noteMotionCommand();
}

void onSlider(lv_event_t* e) {
  int act = (int)(intptr_t)lv_event_get_user_data(e);
  int v = lv_slider_get_value(slider[act]);
  lv_label_set_text_fmt(sliderValue[act], "%d%%", v);
  sliderHoldUntil[act] = millis() + 4000;
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) sendActuator(act, v);
}

void onNudge(lv_event_t* e) {
  int code = (int)(intptr_t)lv_event_get_user_data(e);  // act * 2 + (0 = down, 1 = up)
  int act = code / 2;
  sendActuator(act, lv_slider_get_value(slider[act]) + (code % 2 ? 10 : -10));
}

void onLight(lv_event_t*) {
  bool on = !lv_obj_has_state(lightBtn, LV_STATE_CHECKED);
  setChecked(lightBtn, on);
  lightHoldUntil = millis() + 3000;
  sendCmd(CmdType::Light, SIDE_LEFT, on);
}

void onAdjustStop(lv_event_t*) { stopAll(); }

}  // namespace

// ---- Positions page ------------------------------------------------------------

void buildPresets(lv_obj_t* page) {
  tileRows = 0;  // force the layout pass on the first refresh
  lv_obj_set_style_pad_all(page, 6, 0);
  lv_obj_set_style_pad_gap(page, 8, 0);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
  for (int i = 0; i < kPresetCount; i++) {
    lv_obj_t* t = lv_btn_create(page);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, (kW - 20) / 2, 100);
    lv_obj_set_style_radius(t, 18, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(t, kSurface, 0);
    lv_obj_set_style_bg_color(t, kSurface2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(t, 2, 0);
    lv_obj_set_style_border_color(t, kSurface, 0);
    lv_obj_set_style_border_color(t, kAccent, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(t, lv_color_mix(kAccent, kSurface, 40), LV_STATE_CHECKED);
    lv_obj_add_event_cb(t, onPreset, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    tileIcon[i] = box(t);
    lv_obj_set_size(tileIcon[i], kIconW, kIconH);
    lv_obj_clear_flag(tileIcon[i], LV_OBJ_FLAG_CLICKABLE);
    drawPose(tileIcon[i], i);

    tileLabel[i] = text(t, kPresets[i].label, &lv_font_montserrat_20, kText);
    tiles[i] = t;
  }
}

void refreshPresets() {
  int shown = 0;
  for (int i = 0; i < kPresetCount; i++) shown += presetAvailable(i);
  int rows = (shown + 1) / 2;
  if (rows != tileRows) {  // 2x2 big tiles, or 2x3 for beds with TV/Read
    tileRows = rows;
    int h = (kBodyH - 12 - 8 * (rows - 1)) / rows;
    bool big = h >= 90;
    for (int i = 0; i < kPresetCount; i++) {
      lv_obj_set_height(tiles[i], h);
      lv_obj_align(tileIcon[i], LV_ALIGN_TOP_MID, 0, big ? 16 : 0);
      lv_obj_set_style_text_font(tileLabel[i], big ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
      lv_obj_align(tileLabel[i], LV_ALIGN_BOTTOM_MID, 0, big ? -14 : -3);
    }
  }
  for (int i = 0; i < kPresetCount; i++) {
    setHidden(tiles[i], !presetAvailable(i));
    bool active;
    if (target == Target::Both)
      active = strcmp(shownPreset(SIDE_LEFT), kPresets[i].wire) == 0 && strcmp(shownPreset(SIDE_RIGHT), kPresets[i].wire) == 0;
    else
      active = strcmp(shownPreset(primarySide()), kPresets[i].wire) == 0;
    setChecked(tiles[i], active);
    setLineColor(tileLine[i], active ? kText : kMuted);
    setBgColor(tileHead[i], active ? kText : kMuted);
  }
}

// ---- Firmness page -------------------------------------------------------------

void buildFirmness(lv_obj_t* page) {
  const int cardW = kW - 12, cardH = (kBodyH - 12 - 8) / 2;  // 228 x 100
  for (int s = 0; s < 2; s++) {
    lv_obj_t* card = panel(page, kSurface, 18);
    lv_obj_set_size(card, cardW, cardH);
    lv_obj_set_pos(card, 6, 6 + s * (cardH + 8));

    snName[s] = text(card, sideName((BedSide)s), &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(snName[s], 14, 8);
    snState[s] = text(card, "", &lv_font_montserrat_12, kWarn);
    lv_obj_align(snState[s], LV_ALIGN_TOP_RIGHT, -14, 9);

    // Big number; tap = go to favorite, hold = save as favorite.
    lv_obj_t* num = box(card);
    lv_obj_set_size(num, cardW - 2 * 64, 54);
    lv_obj_align(num, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_add_flag(num, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(num, onSnNumber, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)s);
    lv_obj_add_event_cb(num, onSnNumber, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)s);
    snValue[s] = text(num, "--", &lv_font_montserrat_48, kText);
    lv_obj_center(snValue[s]);
    snHint[s] = text(card, "", &lv_font_montserrat_12, kMuted);
    lv_obj_align(snHint[s], LV_ALIGN_BOTTOM_MID, 0, -8);

    for (int d = 0; d < 2; d++) {
      lv_obj_t* b = button(card, d ? LV_SYMBOL_PLUS : LV_SYMBOL_MINUS, &lv_font_montserrat_20, kSurface2);
      lv_obj_set_size(b, 54, 54);
      lv_obj_align(b, d ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, d ? -8 : 8, 2);
      lv_obj_add_event_cb(b, onSnStep, LV_EVENT_CLICKED, (void*)(intptr_t)(s * 2 + d));
    }
  }
}

void refreshFirmness() {
  for (int s = 0; s < 2; s++) {
    setText(snName[s], sideName((BedSide)s));
    int sn = displayedSn(s);
    bool isPending = pending[s].value >= 0;
    if (sn >= 0) setTextFmt(snValue[s], "%d", sn);
    else setText(snValue[s], "--");
    setTextColor(snValue[s], isPending ? kWarn : kText);
    setText(snState[s], (bed.adjusting[s] || (isPending && pending[s].sent)) ? LV_SYMBOL_REFRESH " air" : "");
    if (isPending && !pending[s].sent) setText(snHint[s], "setting...");
    else if (bed.favorite[s] > 0) setTextFmt(snHint[s], "tap for favorite %d", bed.favorite[s]);
    else setText(snHint[s], "");
  }
}

// Sends debounced Sleep Number changes and clears them once the bed confirms.
void tickFirmness() {
  for (int s = 0; s < 2; s++) {
    PendingSn& p = pending[s];
    if (p.value < 0) continue;
    if (!p.sent && millis() - p.changedAt >= SN_APPLY_DELAY_MS) {
      sendCmd(CmdType::SetSleepNumber, (BedSide)s, p.value);
      p.sent = true;
      p.sentAt = millis();
    } else if (p.sent && millis() - p.sentAt > 3000 &&
               ((bed.sleepNumber[s] == p.value && !bed.adjusting[s]) || millis() - p.sentAt > 90000)) {
      p.value = -1;
    }
  }
}

// ---- Adjust page ---------------------------------------------------------------

void buildAdjust(lv_obj_t* page) {
  const char* names[2] = {"Head", "Foot"};
  const int rowW = kW - 12, btnW = 50, rowH = 44;
  for (int a = 0; a < 2; a++) {
    lv_obj_t* row = box(page);
    lv_obj_set_size(row, rowW, 22 + rowH);
    lv_obj_set_pos(row, 6, 6 + a * 76);

    lv_obj_t* l = text(row, names[a], &lv_font_montserrat_16, kText);
    lv_obj_set_pos(l, 4, 0);
    sliderValue[a] = text(row, "--", &lv_font_montserrat_14, kMuted);
    lv_obj_align(sliderValue[a], LV_ALIGN_TOP_RIGHT, -4, 1);

    for (int d = 0; d < 2; d++) {
      lv_obj_t* b = button(row, d ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, &lv_font_montserrat_16, kSurface);
      lv_obj_set_size(b, btnW, rowH);
      lv_obj_set_pos(b, d ? rowW - btnW : 0, 22);
      lv_obj_add_event_cb(b, onNudge, LV_EVENT_CLICKED, (void*)(intptr_t)(a * 2 + d));
    }

    // Leave room for the knob (radius ~14) between the buttons.
    slider[a] = lv_slider_create(row);
    lv_obj_set_size(slider[a], rowW - 2 * btnW - 2 * 18, 10);
    lv_obj_set_pos(slider[a], btnW + 18, 22 + rowH / 2 - 5);
    lv_slider_set_range(slider[a], 0, 100);
    lv_obj_set_style_bg_color(slider[a], kSurface2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider[a], kAccent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider[a], kText, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider[a], 9, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider[a], 16);
    lv_obj_add_event_cb(slider[a], onSlider, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)a);
    lv_obj_add_event_cb(slider[a], onSlider, LV_EVENT_RELEASED, (void*)(intptr_t)a);
  }

  const int bottomY = kBodyH - 6 - 48, halfW = (kW - 12 - 8) / 2;
  lightBtn = button(page, LV_SYMBOL_POWER "  Light", &lv_font_montserrat_16, kSurface);
  lv_obj_set_size(lightBtn, halfW, 48);
  lv_obj_set_pos(lightBtn, 6, bottomY);
  lv_obj_set_style_bg_color(lightBtn, lv_color_mix(kWarn, kSurface, 70), LV_STATE_CHECKED);
  lv_obj_add_event_cb(lightBtn, onLight, LV_EVENT_CLICKED, nullptr);

  adjustStop = button(page, LV_SYMBOL_STOP "  Stop", &lv_font_montserrat_16, kDanger);
  lv_obj_set_size(adjustStop, halfW, 48);
  lv_obj_set_pos(adjustStop, 6 + halfW + 8, bottomY);
  lv_obj_add_event_cb(adjustStop, onAdjustStop, LV_EVENT_PRESSED, nullptr);
}

void refreshAdjust() {
  BedSide s = primarySide();
  for (int a = 0; a < 2; a++) {
    bool has = bed.hasActuator[s][a];
    if (has) lv_obj_clear_state(slider[a], LV_STATE_DISABLED);
    else lv_obj_add_state(slider[a], LV_STATE_DISABLED);
    int pos = bed.position[s][a];
    bool held = lv_obj_has_state(slider[a], LV_STATE_PRESSED) || millis() < sliderHoldUntil[a];
    if (held) continue;
    if (pos >= 0) {
      lv_slider_set_value(slider[a], pos, LV_ANIM_ON);
      setTextFmt(sliderValue[a], "%d%%", pos);
    } else {
      setText(sliderValue[a], "--");
    }
  }
  setHidden(lightBtn, !bed.hasLight);
  // Keep the optimistic state briefly so the button doesn't flip back before the bed confirms.
  bool lightOn = millis() < lightHoldUntil ? lv_obj_has_state(lightBtn, LV_STATE_CHECKED) : bed.lightOn;
  setChecked(lightBtn, lightOn);
  setTextColor(lv_obj_get_child(lightBtn, 0), lightOn ? kWarn : kText);
  const int halfW = (kW - 12 - 8) / 2;
  setWidth(adjustStop, bed.hasLight ? halfW : kW - 12);
  setX(adjustStop, bed.hasLight ? 6 + halfW + 8 : 6);
}

}  // namespace ui
