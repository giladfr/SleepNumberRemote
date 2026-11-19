#pragma once
// Build-time defaults. Personal overrides go in include/config_local.h (git-ignored,
// see config_local.example.h); anything set there wins over the values below.
// Names, side swap, brightness and screen timeout can also be changed on the device
// (Settings) and are saved in its flash. The paired bed's address and Bluetooth keys
// only ever live in the device's flash.

#if __has_include("config_local.h")
#include "config_local.h"
#endif

#define FW_VERSION "1.4.0"

// ---- Defaults for on-device settings ------------------------------------------
#ifndef LEFT_NAME
#define LEFT_NAME "Left"
#endif
#ifndef RIGHT_NAME
#define RIGHT_NAME "Right"
#endif
// 1 = the bed's own "left"/"right" is the opposite of the on-screen sides.
#ifndef SWAP_BED_SIDES
#define SWAP_BED_SIDES 0
#endif
// Portrait with the USB port at the bottom; 1 = upside down (USB on top).
#ifndef FLIP_SCREEN
#define FLIP_SCREEN 0
#endif
#ifndef BACKLIGHT_ON
#define BACKLIGHT_ON 255
#endif
#ifndef OFF_AFTER_MS
#define OFF_AFTER_MS 90000  // screen off + Bluetooth released for the phone app
#endif

// ---- Behaviour --------------------------------------------------------------
// Under-bed light auto-off timer (minutes) when turned on from the remote.
#ifndef LIGHT_TIMER_MIN
#define LIGHT_TIMER_MIN 15
#endif
// Wait this long after the last +/- tap before sending the new Sleep Number.
#ifndef SN_APPLY_DELAY_MS
#define SN_APPLY_DELAY_MS 1200
#endif
#ifndef BACKLIGHT_DIM
#define BACKLIGHT_DIM 30
#endif
#ifndef DIM_AFTER_MS
#define DIM_AFTER_MS 30000
#endif

// ---- Touch calibration (XPT2046 raw values) --------------------------------
#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 200
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3700
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 240
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3800
#endif
