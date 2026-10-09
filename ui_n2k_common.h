// esp32-S3-ws4-boat v1.0
// ui_n2k_common.h - shared tile widget, fonts and colours for the N2K tabs (LVGL 8.x)
#pragma once
#include "app.h"  // tr() and the translating LVGL wrappers, ui_font()

/* The built-in fonts; ui_font() swaps in the Latin-1 versions when a language
   other than English is chosen, so a-ring and umlauts show */
extern const lv_font_t *ui_font_xl_builtin;  // big numbers (48 if enabled in lv_conf.h)
extern const lv_font_t *ui_font_l_builtin;   // 28
extern const lv_font_t *ui_font_m_builtin;   // 20
extern const lv_font_t *ui_font_s_builtin;   // 14
#define UI_FONT_XL ui_font(ui_font_xl_builtin)
#define UI_FONT_L ui_font(ui_font_l_builtin)
#define UI_FONT_M ui_font(ui_font_m_builtin)
#define UI_FONT_S ui_font(ui_font_s_builtin)

enum UiLevel { UI_OK, UI_WARN, UI_ALARM, UI_STALE };

struct UiTile { lv_obj_t *box, *title, *value, *unit; };

lv_color_t uiLevelColor(UiLevel l);
void       uiPrepTab(lv_obj_t *tab);   // dark background, flex wrap
UiTile     uiTile(lv_obj_t *parent, const char *title, const char *unit,
                  const lv_font_t *valueFont, lv_coord_t w, lv_coord_t h);
void       uiTileSet(UiTile &t, const char *text, UiLevel level);
void       uiLabelSetIfChanged(lv_obj_t *lbl, const char *text);  // translated, like set_label()

struct N2kTank;
/* A tank's name for the screen: the default names ("Fresh water", "Grey water 2")
   in the chosen language, a name set by the user as typed. short_name: the first
   word only ("Fresh"), for the start page. */
void       uiTankName(const N2kTank *t, char *buf, size_t n, bool short_name = false);
