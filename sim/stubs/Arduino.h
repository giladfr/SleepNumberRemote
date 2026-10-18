// Minimal Arduino shim so the UI code and lv_conf.h compile on the desktop.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);  // simulated clock, see sim_main.cpp
#ifdef __cplusplus
}
#endif
