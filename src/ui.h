#pragma once

#include <stdint.h>

namespace Ui {
void build();  // create all screens; call once after LVGL + display init
}

// Implemented by the platform (device or simulator).
namespace Platform {
void setBrightness(uint8_t level);
void applyRotation();  // after Settings.flipScreen changes
}
