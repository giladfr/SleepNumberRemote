// Sleep Number remote for the ESP32-2432S028R "Cheap Yellow Display", portrait.
// Display: ILI9341 on HSPI (TFT_eSPI, pins in platformio.ini).
// Touch:   XPT2046 on VSPI (CLK 25, MISO 39, MOSI 32, CS 33, IRQ 36).

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "bed_link.h"
#include "config.h"
#include "homekit.h"
#include "settings.h"
#include "ui.h"

namespace {

constexpr int kWidth = 240;  // portrait
constexpr int kHeight = 320;
constexpr int kBacklightChannel = 0;
constexpr int kRgbLedPins[] = {4, 16, 17};

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(33, 36);

lv_disp_draw_buf_t drawBuf;
// One buffer: flush() waits for the SPI transfer, so a second one wouldn't overlap
// anything, and its 11.5 KB is better spent on Wi-Fi/HomeKit.
lv_color_t buf1[kWidth * 24];

enum class Power { On, Dim, Off } power = Power::On;
bool swallowTouch = false;
uint32_t offSince = 0;
uint32_t restartAt = 0;

// Survives ESP.restart() (not a power cycle): what to do after a planned restart.
RTC_NOINIT_ATTR uint32_t bootFlags;
constexpr uint32_t kBootMagic = 0xB0070000;
constexpr uint32_t kBootQuiet = 1;         // restarted while the screen was off: stay dark
constexpr uint32_t kBootOpenHomeKit = 2;   // restarted from the HomeKit screen: go back there

void restartWith(uint32_t flags) {
  bootFlags = kBootMagic | flags;
  Serial.flush();
  ESP.restart();
}

void setBacklight(uint8_t level) { ledcWrite(kBacklightChannel, level); }

void wake() {
  setBacklight(Settings::get().brightness);
  power = Power::On;
  lv_disp_trig_activity(nullptr);
  BedLink::setActive(true);
}

void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* colors) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t*)&colors->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  bool touched = touch.tirqTouched() && touch.touched();
  // A touch on a dark screen only wakes it, so nothing is pressed blindly.
  // A dimmed screen is still readable, so that touch goes through.
  if (touched && power != Power::On) {
    swallowTouch = power == Power::Off;
    wake();
  }
  if (swallowTouch || !touched) {
    if (!touched) swallowTouch = false;
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  // Touch is calibrated in landscape (display rotation 1). Convert to the portrait
  // frame using the ILI9341 MADCTL relation between rotations 1 (MV), 0 (MX), 2 (MY).
  TS_Point p = touch.getPoint();
  int lx = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, kHeight - 1), 0, kHeight - 1);
  int ly = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, kWidth - 1), 0, kWidth - 1);
  if (Settings::get().flipScreen) {
    data->point.x = ly;
    data->point.y = kHeight - 1 - lx;
  } else {
    data->point.x = kWidth - 1 - ly;
    data->point.y = lx;
  }
  data->state = LV_INDEV_STATE_PR;
}

}  // namespace

void Platform::setBrightness(uint8_t level) {
  if (power == Power::On) setBacklight(level);
}

void Platform::applyRotation() {
  tft.setRotation(Settings::get().flipScreen ? 2 : 0);
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
}

void setup() {
  Serial.begin(115200);
  Serial.printf("[boot] reset reason %d\n", (int)esp_reset_reason());  // 4 = crash, 6 = task watchdog, 9 = brownout
  uint32_t flags = (bootFlags & 0xFFFF0000) == kBootMagic ? bootFlags & 0xFFFF : 0;
  bootFlags = 0;
  Settings::load();
  for (int pin : kRgbLedPins) {  // on-board RGB LED is active-low; keep it dark
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  tft.init();
  tft.setRotation(Settings::get().flipScreen ? 2 : 0);
  tft.fillScreen(TFT_BLACK);
  ledcSetup(kBacklightChannel, 5000, 8);
  ledcAttachPin(TFT_BL, kBacklightChannel);
  setBacklight(flags & kBootQuiet ? 0 : Settings::get().brightness);

  touchSpi.begin(25, 39, 32, 33);
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, kWidth * 24);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kWidth;
  dispDrv.ver_res = kHeight;
  dispDrv.flush_cb = flush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_t* disp = lv_disp_drv_register(&dispDrv);
  lv_theme_default_init(disp, lv_color_hex(0x5B8CFF), lv_color_hex(0x34D399), true, &lv_font_montserrat_14);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = readTouch;
  lv_indev_drv_register(&indevDrv);

  BedLink::begin();  // before the UI: the UI reads bed state while building
  Ui::build();
  Serial.printf("[mem] free after start: %u\n", (unsigned)ESP.getFreeHeap());
  HomeKit::begin();
  if (flags & kBootOpenHomeKit) Ui::openHomeKit();
  if (flags & kBootQuiet) {
    power = Power::Off;
    offSince = millis();
    BedLink::setActive(false);
  }
}

void loop() {
  lv_timer_handler();

  uint32_t idle = lv_disp_get_inactive_time(nullptr);
  uint32_t offMs = Settings::get().screenOffSec * 1000UL;  // 0 = never
  uint32_t dimMs = offMs ? min<uint32_t>(DIM_AFTER_MS, offMs / 2) : DIM_AFTER_MS;
  if (power == Power::On && idle > dimMs) {
    setBacklight(BACKLIGHT_DIM);
    power = Power::Dim;
  }
  if (offMs && power != Power::Off && idle > offMs) {
    setBacklight(0);
    power = Power::Off;
    offSince = millis();
    BedLink::setActive(false);
  }

  HomeKit::loop();
  switch (HomeKit::pendingRestart()) {
    case HomeKit::Restart::Now:  // a HomeKit setting changed; the screen says "Restarting"
      if (!restartAt) restartAt = millis() + 1500;
      else if ((int32_t)(millis() - restartAt) >= 0) restartWith(kBootOpenHomeKit);
      break;
    case HomeKit::Restart::WhenIdle:  // names or bed features changed: apply unnoticed
      if (power == Power::Off && millis() - offSince > 120000) restartWith(kBootQuiet);
      break;
    case HomeKit::Restart::None: break;
  }
  delay(5);
}
