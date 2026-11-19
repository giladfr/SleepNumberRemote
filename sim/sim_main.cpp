// Desktop simulator: runs the real UI code against a fake bed, drives it with
// scripted touches and saves screenshots (sim/out/*.png, 2x scale).
//
//   make -C sim run

#include <Arduino.h>
#include <lvgl.h>

#include <string>

#include "sim_bed.h"
#include "sim_homekit.h"
#include "ui.h"

namespace {

constexpr int kW = 240, kH = 320, kScale = 2;  // portrait
uint32_t simNow = 0;
lv_color_t framebuffer[kW * kH];
struct {
  int x = 0, y = 0;
  bool pressed = false;
} pointer;
uint8_t brightness = 255;

void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* colors) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) framebuffer[y * kW + x] = *colors++;
  lv_disp_flush_ready(drv);
}

void readPointer(lv_indev_drv_t*, lv_indev_data_t* data) {
  data->point.x = pointer.x;
  data->point.y = pointer.y;
  data->state = pointer.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 5) {
    simNow += 5;
    lv_timer_handler();
  }
}

void tapAt(int x, int y, uint32_t holdMs = 80) {
  pointer = {x, y, true};
  run(holdMs);
  pointer.pressed = false;
  run(250);
}

void drag(int x, int y1, int y2) {
  pointer = {x, y1, true};
  run(40);
  for (int i = 1; i <= 20; i++) {
    pointer.y = y1 + (y2 - y1) * i / 20;
    run(15);
  }
  pointer.pressed = false;
  run(800);
}

// ---- Find widgets by what is written on them, so the script survives layout changes.

bool shown(lv_obj_t* o) {
  for (; o; o = lv_obj_get_parent(o))
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  return true;
}

lv_obj_t* findLabel(lv_obj_t* root, const char* txt, bool prefix, int& nth) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); i++) {
    lv_obj_t* c = lv_obj_get_child(root, i);
    if (!shown(c)) continue;
    if (lv_obj_check_type(c, &lv_label_class)) {
      const char* t = lv_label_get_text(c);
      bool match = prefix ? strncmp(t, txt, strlen(txt)) == 0 : strcmp(t, txt) == 0;
      if (match && nth-- == 0) return c;
    }
    if (lv_obj_t* r = findLabel(c, txt, prefix, nth)) return r;
  }
  return nullptr;
}

lv_obj_t* findClass(lv_obj_t* root, const lv_obj_class_t* cls) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); i++) {
    lv_obj_t* c = lv_obj_get_child(root, i);
    if (!shown(c)) continue;
    if (lv_obj_check_type(c, cls)) return c;
    if (lv_obj_t* r = findClass(c, cls)) return r;
  }
  return nullptr;
}

// Tap the clickable widget that carries this label (nth match, overlays first).
void tap(const char* txt, int nth = 0, bool prefix = false, uint32_t holdMs = 80) {
  lv_obj_t* roots[] = {lv_layer_top(), lv_scr_act()};
  lv_obj_t* label = nullptr;
  for (lv_obj_t* root : roots) {
    // Overlays are the last children of the screen; search them first.
    for (int i = (int)lv_obj_get_child_cnt(root) - 1; i >= 0 && !label; i--) {
      int n = nth;
      lv_obj_t* c = lv_obj_get_child(root, i);
      if (shown(c)) label = findLabel(c, txt, prefix, n);
    }
    if (label) break;
  }
  if (!label) {
    fprintf(stderr, "  !! no visible widget labelled \"%s\"\n", txt);
    return;
  }
  lv_obj_t* target = label;
  while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
  if (!target) target = label;
  lv_obj_scroll_to_view_recursive(target, LV_ANIM_OFF);
  run(60);
  lv_area_t a;
  lv_obj_get_coords(target, &a);
  tapAt((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, holdMs);
}

// Tap a key of the visible keyboard / button matrix (e.g. a dialog's buttons).
void tapKey(const char* txt) {
  lv_obj_t* m = findClass(lv_layer_top(), &lv_btnmatrix_class);
  if (!m) m = findClass(lv_scr_act(), &lv_keyboard_class);
  if (!m) m = findClass(lv_scr_act(), &lv_btnmatrix_class);
  if (!m) {
    fprintf(stderr, "  !! no keyboard for key \"%s\"\n", txt);
    return;
  }
  lv_btnmatrix_t* bm = (lv_btnmatrix_t*)m;
  for (uint16_t i = 0, btn = 0; bm->map_p[i][0]; i++) {
    if (strcmp(bm->map_p[i], "\n") == 0) continue;
    if (strcmp(bm->map_p[i], txt) == 0) {
      lv_area_t a = bm->button_areas[btn];
      tapAt(m->coords.x1 + (a.x1 + a.x2) / 2, m->coords.y1 + (a.y1 + a.y2) / 2);
      return;
    }
    btn++;
  }
  fprintf(stderr, "  !! no key \"%s\"\n", txt);
}

// Toggle the switch in the list row carrying this label.
void tapSwitch(const char* rowLabel) {
  int n = 0;
  lv_obj_t* label = nullptr;
  for (int i = (int)lv_obj_get_child_cnt(lv_scr_act()) - 1; i >= 0 && !label; i--) {
    lv_obj_t* c = lv_obj_get_child(lv_scr_act(), i);
    if (shown(c)) label = findLabel(c, rowLabel, false, n);
  }
  lv_obj_t* sw = label ? findClass(lv_obj_get_parent(label), &lv_switch_class) : nullptr;
  if (!sw) {
    fprintf(stderr, "  !! no switch in row \"%s\"\n", rowLabel);
    return;
  }
  lv_obj_scroll_to_view_recursive(sw, LV_ANIM_OFF);
  run(60);
  lv_area_t a;
  lv_obj_get_coords(sw, &a);
  tapAt((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

// 24-bit BMP at kScale, converted to PNG with macOS `sips`.
void shot(const char* name, const char* caption) {
  run(400);  // let animations settle
  const int w = kW * kScale, h = kH * kScale, rowBytes = w * 3;
  std::string bmp = std::string("out/") + name + ".bmp";
  FILE* f = fopen(bmp.c_str(), "wb");
  uint32_t fileSize = 54 + rowBytes * h;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 0xFF; };
  put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  hdr[26] = 1; hdr[28] = 24; put32(34, rowBytes * h);
  fwrite(hdr, 1, 54, f);
  std::string row(rowBytes, 0);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      uint16_t c = framebuffer[(y / kScale) * kW + x / kScale].full;
      uint8_t r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
      r = r * brightness / 255; g = g * brightness / 255; b = b * brightness / 255;
      row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
    }
    fwrite(row.data(), 1, rowBytes, f);
  }
  fclose(f);
  std::string cmd = "sips -s format png " + bmp + " --out out/" + name + ".png >/dev/null && rm " + bmp;
  if (system(cmd.c_str()) != 0) fprintf(stderr, "sips failed for %s\n", name);
  printf("%-24s %s\n", name, caption);
}

}  // namespace

extern "C" uint32_t millis(void) { return simNow; }
void Platform::setBrightness(uint8_t level) { brightness = level; }
void Platform::applyRotation() {}  // the UI looks the same either way up

int main() {
  system("mkdir -p out && rm -f out/*.png");
  SimBed::reset();

  lv_init();
  static lv_disp_draw_buf_t drawBuf;
  static lv_color_t buf[kW * 40];
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, kW * 40);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kW;
  dispDrv.ver_res = kH;
  dispDrv.flush_cb = flush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_t* disp = lv_disp_drv_register(&dispDrv);
  lv_theme_default_init(disp, lv_color_hex(0x5B8CFF), lv_color_hex(0x34D399), true, &lv_font_montserrat_14);
  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = readPointer;
  lv_indev_drv_register(&indevDrv);

  Ui::build();
  run(300);

  shot("01_positions", "Home: positions for the left sleeper");
  tap("Sam");
  shot("02_positions_right", "Right sleeper selected (Snore active)");
  tap("Both");
  tap("Zero G");
  shot("03_zero_g_both_moving", "Both + Zero G: STOP bar while moving");
  run(5000);
  shot("04_positions_after", "Motion finished");
  tap("Alex");

  tap("Firmness");
  shot("05_firmness", "Firmness: both sleepers");
  tap(LV_SYMBOL_PLUS);
  tap(LV_SYMBOL_PLUS);
  shot("06_firmness_pending", "Left +10 pending (orange)");
  run(1500);
  shot("07_firmness_adjusting", "Sent, pump adjusting");

  tap("Adjust");
  shot("08_adjust", "Adjust head/foot + light");

  tap("Settings");
  shot("09_settings", "Settings");
  drag(120, 220, 80);
  shot("10_settings_scrolled", "Settings scrolled");

  tap("Names");
  shot("10b_names", "Names editor");
  tap(LV_SYMBOL_LEFT);
  tap("Pair a bed");
  run(300);
  shot("11_pair_scanning", "Pairing: scanning");
  run(2500);
  shot("12_pair_found", "Pairing: beds found");
  tap("Smart bed 789ABC", 0, true);
  shot("13_pair_wait", "Pairing: hold the bed button");
  run(5500);
  shot("14_pair_done", "Pairing: done");
  tap("Done");

  tap("Bed info & data");
  shot("15_info", "Bed info: connection");
  drag(120, 300, 80);
  shot("16_info_bed", "Bed info: bed + sides");
  drag(120, 300, 60);
  drag(120, 300, 60);
  shot("17_info_log", "Bed info: command log");
  tap(LV_SYMBOL_LEFT);

  tap("Forget pairing");
  shot("18_forget_dialog", "Forget pairing confirm");
  tapKey("Cancel");

  // Rename Alex -> Al with the on-screen keyboard (backspace x2, then the check key).
  tap("Names");
  tapKey(LV_SYMBOL_BACKSPACE);
  tapKey(LV_SYMBOL_BACKSPACE);
  shot("19_names_editing", "Names: editing the left name");
  tapKey(LV_SYMBOL_OK);
  shot("20_settings_renamed", "Settings after rename");
  tap("Position");
  shot("21_positions_renamed", "Header uses the new name");
  tap("Firmness");
  shot("22_firmness_renamed", "Firmness uses the new name");
  tap("Position");

  SimBed::state().presetTv = SimBed::state().presetRead = true;
  SimBed::state().version++;
  run(300);
  shot("23_positions_six", "A bed with all six presets");
  SimBed::state().presetTv = SimBed::state().presetRead = false;

  SimBed::state().link = LinkState::NeedsPairing;
  strlcpy(SimBed::state().status, "Pairing needed", sizeof(SimBed::state().status));
  SimBed::state().version++;
  shot("24_needs_pairing", "Home when the bed needs pairing");
  SimBed::reset();
  run(300);

  // HomeKit setup: enable, pick Wi-Fi, type the password, scan the code.
  tap("Settings");
  tap("HomeKit");
  shot("25_homekit_off", "HomeKit: off");
  tapSwitch("Apple Home");
  run(300);
  shot("26_wifi_scanning", "Enabling asks for Wi-Fi: scanning");
  run(1600);
  shot("27_wifi_list", "Wi-Fi networks");
  tap("HomeNet");
  for (const char* k : {"s", "e", "c", "r", "e", "t"}) tapKey(k);
  shot("28_password", "Wi-Fi password (hidden)");
  tap(LV_SYMBOL_EYE_OPEN);
  tapKey("1#");
  tapKey("4");
  tapKey("2");
  shot("29_password_symbols", "Password shown, symbol keys");
  tapKey(LV_SYMBOL_OK);
  run(300);
  shot("30_homekit_connecting", "HomeKit: joining Wi-Fi");
  run(2500);
  shot("31_homekit_code", "HomeKit: setup code to scan");
  SimHomeKit::pairPhone();
  run(600);
  shot("32_homekit_paired", "HomeKit: added to Apple Home");
  tapSwitch("Local API");  // off and on again: scrolls the row into view
  run(200);
  tapSwitch("Local API");
  run(200);
  shot("32a_homekit_api", "HomeKit: Wi-Fi, local API, reset");
  tap("Reset HomeKit");
  shot("33_homekit_reset", "Reset HomeKit confirm");
  tapKey("Cancel");
  tap(LV_SYMBOL_LEFT);
  shot("34_settings_homekit", "Settings with HomeKit on");
  tap("HomeKit");
  tapSwitch("Apple Home");
  run(300);
  shot("35_restarting", "Turning HomeKit off restarts the remote");

  return 0;
}
