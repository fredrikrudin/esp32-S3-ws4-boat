// ui_n2k_common.h - shared tile widget, fonts and colours for the N2K tabs (LVGL 8.x)
#pragma once
#include <lvgl.h>

extern const lv_font_t *UI_FONT_XL;   // big numbers (48 if enabled in lv_conf.h)
extern const lv_font_t *UI_FONT_L;    // 28
extern const lv_font_t *UI_FONT_M;    // 20
extern const lv_font_t *UI_FONT_S;    // 14

enum UiLevel { UI_OK, UI_WARN, UI_ALARM, UI_STALE };

struct UiTile { lv_obj_t *box, *title, *value, *unit; };

lv_color_t uiLevelColor(UiLevel l);
void       uiPrepTab(lv_obj_t *tab);   // dark background, flex wrap
UiTile     uiTile(lv_obj_t *parent, const char *title, const char *unit,
                  const lv_font_t *valueFont, lv_coord_t w, lv_coord_t h);
void       uiTileSet(UiTile &t, const char *text, UiLevel level);
void       uiLabelSetIfChanged(lv_obj_t *lbl, const char *text);
