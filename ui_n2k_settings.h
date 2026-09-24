// ui_n2k_settings.h - NMEA 2000 settings tab
//
// Lists every device (source address) seen on the bus, grouped by the PGNs it
// sends, with the values decoded from each PGN. Tap a value to edit its gauge
// range and warning/alarm limits (saved in NVS, used by all tabs immediately).
#pragma once
#include <lvgl.h>

// Builds the list into a tab (or any scrollable parent).
void ui_n2k_settings_create(lv_obj_t *tab);

// Opens the list as a full screen with a Back button (used from Settings).
void ui_n2k_show_screen();

// Closes the limit editor if it is open (the screen saver calls this).
void ui_n2k_close_editor();
