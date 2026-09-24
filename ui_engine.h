// ui_engine.h - Engine tab (one engine per display; choose instance in the header dropdown)
#pragma once
#include <lvgl.h>

// Builds the tab contents and starts its own 250 ms LVGL update timer.
// Call from the same place/lock where the other tabs are created.
void ui_engine_create(lv_obj_t *tab);

// true while the Engine tab is on screen (and, with ENG_KEEP_AWAKE 2, the engine runs).
// The tab also resets LVGL's inactivity timer itself, so a screensaver based on
// lv_disp_get_inactive_time() needs no change. Use this if your screensaver keeps
// its own timer instead.
bool ui_engine_keep_awake();
