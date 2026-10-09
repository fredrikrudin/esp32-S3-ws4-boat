// esp32-S3-ws4-boat v1.0
// ui_n2k_common.cpp
#include <string.h>
#include "ui_n2k_common.h"
#include "n2k_data.h"

// Pick the largest fonts enabled in lv_conf.h (LV_FONT_MONTSERRAT_xx 1)
#if LV_FONT_MONTSERRAT_48
const lv_font_t *ui_font_xl_builtin = &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_40
const lv_font_t *ui_font_xl_builtin = &lv_font_montserrat_40;
#elif LV_FONT_MONTSERRAT_32
const lv_font_t *ui_font_xl_builtin = &lv_font_montserrat_32;
#else
const lv_font_t *ui_font_xl_builtin = LV_FONT_DEFAULT;
#endif

#if LV_FONT_MONTSERRAT_28
const lv_font_t *ui_font_l_builtin = &lv_font_montserrat_28;
#elif LV_FONT_MONTSERRAT_24
const lv_font_t *ui_font_l_builtin = &lv_font_montserrat_24;
#else
const lv_font_t *ui_font_l_builtin = LV_FONT_DEFAULT;
#endif

#if LV_FONT_MONTSERRAT_20
const lv_font_t *ui_font_m_builtin = &lv_font_montserrat_20;
#elif LV_FONT_MONTSERRAT_18
const lv_font_t *ui_font_m_builtin = &lv_font_montserrat_18;
#else
const lv_font_t *ui_font_m_builtin = LV_FONT_DEFAULT;
#endif

#if LV_FONT_MONTSERRAT_14
const lv_font_t *ui_font_s_builtin = &lv_font_montserrat_14;
#else
const lv_font_t *ui_font_s_builtin = LV_FONT_DEFAULT;
#endif

static const uint32_t COL_BG    = 0x0B1622;
static const uint32_t COL_TILE  = 0x16263A;
static const uint32_t COL_TITLE = 0x8FA3B8;

lv_color_t uiLevelColor(UiLevel l) {
  switch (l) {
    case UI_WARN:  return lv_palette_main(LV_PALETTE_ORANGE);
    case UI_ALARM: return lv_palette_main(LV_PALETTE_RED);
    case UI_STALE: return lv_color_hex(0x5A6B7D);
    default:       return lv_color_white();
  }
}

void uiPrepTab(lv_obj_t *tab) {
  lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(tab, 8, 0);
  lv_obj_set_style_pad_row(tab, 8, 0);
  lv_obj_set_style_pad_column(tab, 8, 0);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

UiTile uiTile(lv_obj_t *parent, const char *title, const char *unit,
              const lv_font_t *valueFont, lv_coord_t w, lv_coord_t h) {
  UiTile t;
  t.box = lv_obj_create(parent);
  lv_obj_set_size(t.box, w, h);
  lv_obj_clear_flag(t.box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(t.box, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_border_width(t.box, 0, 0);
  lv_obj_set_style_radius(t.box, 10, 0);
  lv_obj_set_style_pad_all(t.box, 6, 0);

  t.title = lv_label_create(t.box);
  lv_label_set_text(t.title, title);
  lv_obj_set_style_text_font(t.title, UI_FONT_S, 0);
  lv_obj_set_style_text_color(t.title, lv_color_hex(COL_TITLE), 0);
  lv_obj_align(t.title, LV_ALIGN_TOP_LEFT, 0, 0);

  t.value = lv_label_create(t.box);
  lv_label_set_text(t.value, "--");
  lv_obj_set_style_text_font(t.value, valueFont, 0);
  lv_obj_set_style_text_color(t.value, uiLevelColor(UI_STALE), 0);
  lv_obj_align(t.value, LV_ALIGN_CENTER, 0, 6);

  t.unit = lv_label_create(t.box);
  lv_label_set_text(t.unit, unit);
  lv_obj_set_style_text_font(t.unit, UI_FONT_S, 0);
  lv_obj_set_style_text_color(t.unit, lv_color_hex(COL_TITLE), 0);
  lv_obj_align(t.unit, LV_ALIGN_TOP_RIGHT, 0, 0);
  return t;
}

void uiLabelSetIfChanged(lv_obj_t *lbl, const char *text) {
  set_label(lbl, text);  // translates, then redraws only when the text changed
}

/* the start page's short names for the default tanks, translated */
static const char *const FLUID_SHORT[] __attribute__((unused)) = {
  N_("Fuel"), N_("Fresh"), N_("Grey"), N_("Live"), N_("Oil"), N_("Black"), N_("Gasoline"), N_("Tank")
};

void uiTankName(const N2kTank *t, char *buf, size_t n, bool short_name) {
  if (!t) {
    if (n) buf[0] = 0;
    return;
  }
  const char *fluid = n2kFluidName(t->fluidType);
  char auto_name[32];
  snprintf(auto_name, sizeof(auto_name), "%s %u", fluid, t->instance + 1);
  bool is_default = !strcmp(t->name, fluid) || !strcmp(t->name, auto_name);
  if (!short_name) {
    if (!strcmp(t->name, auto_name)) snprintf(buf, n, "%s %u", tr(fluid), t->instance + 1);  // "Fresh water 2"
    else strlcpy(buf, tr(t->name), n);
    return;
  }
  /* first word: "Fresh water" -> "Fresh", translated as a word of its own */
  char word[24];
  strlcpy(word, t->name, sizeof(word));
  char *sp = strchr(word, ' ');
  if (sp) *sp = 0;
  strlcpy(buf, is_default ? tr(word) : word, n);
}

void uiTileSet(UiTile &t, const char *text, UiLevel level) {
  uiLabelSetIfChanged(t.value, text);
  lv_obj_set_style_text_color(t.value, uiLevelColor(level), 0);
  // Alarm: tint the whole tile so it is visible from across the cockpit
  lv_obj_set_style_bg_color(t.box, level == UI_ALARM ? lv_color_hex(0x5A1414) : lv_color_hex(COL_TILE), 0);
}
