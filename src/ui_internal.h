#pragma once
// Shared pieces of the UI: palette, layout constants, widget helpers and the
// per-page build/refresh entry points.

#include <lvgl.h>

#include "bed_link.h"

namespace ui {

// ---- Palette ---------------------------------------------------------------
extern const lv_color_t kBg, kSurface, kSurface2, kLine;
extern const lv_color_t kAccent, kOk, kWarn, kDanger, kText, kMuted;

// ---- Layout (portrait 240x320, 2.8" ~ 5.6 px/mm: 44 px ~ 8 mm finger target) --
constexpr int kW = 240;
constexpr int kH = 320;
constexpr int kHeaderH = 46;
constexpr int kNavH = 54;
constexpr int kBodyH = kH - kHeaderH - kNavH;  // 220

// ---- Shared state ------------------------------------------------------------
enum class Target : uint8_t { Left, Both, Right };
extern Target target;  // which side presets/adjust apply to
extern BedState bed;   // latest snapshot

inline BedSide primarySide() { return target == Target::Right ? SIDE_RIGHT : SIDE_LEFT; }
inline bool targets(BedSide s) {
  return target == Target::Both || (s == SIDE_LEFT) == (target == Target::Left);
}
const char* sideName(BedSide s);

// ---- Helpers -----------------------------------------------------------------
lv_obj_t* box(lv_obj_t* parent);  // invisible, non-scrolling container
lv_obj_t* panel(lv_obj_t* parent, lv_color_t bg, int radius);
lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, lv_color_t color);
lv_obj_t* button(lv_obj_t* parent, const char* label, const lv_font_t* font, lv_color_t bg);
void setHidden(lv_obj_t* o, bool hidden);
void setChecked(lv_obj_t* o, bool checked);
// Change-only setters: refreshes run often, and every LVGL setter call would
// otherwise invalidate (redraw) the widget even when nothing changed.
void setText(lv_obj_t* label, const char* s);
void setTextFmt(lv_obj_t* label, const char* fmt, ...);
void setTextColor(lv_obj_t* o, lv_color_t c);
void setBgColor(lv_obj_t* o, lv_color_t c);
void setLineColor(lv_obj_t* o, lv_color_t c);
void setWidth(lv_obj_t* o, lv_coord_t w);
void setX(lv_obj_t* o, lv_coord_t x);

void sendCmd(CmdType type, BedSide side, int value = 0, Actuator act = ACT_HEAD, const char* preset = nullptr);
void noteMotionCommand();  // shows STOP right away, before the bed reports motion
void stopAll();
void goToPage(int page);

enum Page { kPagePresets, kPageFirmness, kPageAdjust, kPageSettings, kPageCount };

// ---- Pages -------------------------------------------------------------------
void buildPresets(lv_obj_t* page);
void refreshPresets();
void buildFirmness(lv_obj_t* page);
void refreshFirmness();
void tickFirmness();
void buildAdjust(lv_obj_t* page);
void refreshAdjust();
void buildSettings(lv_obj_t* page);
void refreshSettings();

// Full-screen overlays
void buildOverlays(lv_obj_t* scr);
void refreshOverlays();
void openPairing(bool scanFirst);
void openInfo();
void openNames();

}  // namespace ui
