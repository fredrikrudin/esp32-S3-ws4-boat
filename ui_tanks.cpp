// ui_tanks.cpp - Tanks tab, vertical "card" gauges
//
//   +-----------+   Coloured frame per fluid type (red on alarm)
//   |Fresh water|
//   | +-------+ |   Segmented vertical bar (quarters)
//   | |       | |
//   | |-------| |
//   | |#######| |
//   | |#######| |
//   | +-------+ |
//   |   45 %    |
//   |  8/18 L   |   shown when the sender reports capacity
//   +-----------+
//
// Up to 3 cards fill the width; with more tanks the cards get narrower and
// from 5 tanks on the row scrolls sideways.
// Cards are rebuilt automatically when a tank appears on the bus or is renamed.
#include <stdio.h>
#include "ui_tanks.h"
#include "ui_n2k_common.h"
#include "n2k_config.h"
#include "n2k_data.h"
#include "n2k_limits.h"
#include "ui_gauge.h"

struct TankCard {
  lv_obj_t *card, *name, *well, *fill, *pct, *unit, *litres;
  uint8_t ft, inst;
};

static lv_obj_t *s_tab;
static TankCard s_cards[N2K_MAX_TANKS];
static int s_count = 0;
static uint32_t s_builtVer = 0;

static const uint32_t COL_CARD_BG = 0x151515;
static const uint32_t COL_WELL    = 0x22354D;   // empty part of the bar
static const int      SEGMENTS    = 4;

// Levels come from the tank's limits (editable in the N2K settings tab)
static UiLevel tankLevel(uint8_t ft, uint8_t inst, double pct) {
  return uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_TANK_LEVEL, inst, ft), pct));
}

// Frame colour identifies the fluid type
static lv_color_t frameColor(uint8_t ft) {
  switch (ft) {
    case 1:  return lv_color_hex(0x8ECDF2);   // fresh water: light blue
    case 2:  return lv_color_hex(0x2BB5D6);   // grey water: teal
    case 5:  return lv_color_hex(0x9C7A5B);   // black water: brown
    case 0:
    case 6:  return lv_color_hex(0xE0A93A);   // fuel: amber
    default: return lv_color_hex(0x7FA0BF);
  }
}

static lv_color_t fillColor(uint8_t ft, UiLevel l) {
  if (l == UI_ALARM) return lv_palette_main(LV_PALETTE_RED);
  if (l == UI_WARN)  return lv_palette_main(LV_PALETTE_ORANGE);
  switch (ft) {
    case 5:  return lv_color_hex(0x8A6A4C);
    case 0:
    case 6:  return lv_color_hex(0xD39A2C);
    default: return lv_color_hex(0x3A7EC6);   // water: blue, as in the reference
  }
}

static lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

static void makeCard(TankCard &c, const N2kTank *t, lv_coord_t w) {
  c.ft = t->fluidType;
  c.inst = t->instance;

  c.card = lv_obj_create(s_tab);
  lv_obj_set_size(c.card, w, lv_pct(100));
  lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(c.card, lv_color_hex(COL_CARD_BG), 0);
  lv_obj_set_style_bg_opa(c.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c.card, 3, 0);
  lv_obj_set_style_border_color(c.card, frameColor(c.ft), 0);
  lv_obj_set_style_radius(c.card, 14, 0);
  lv_obj_set_style_pad_all(c.card, 10, 0);
  lv_obj_set_style_pad_row(c.card, 6, 0);
  lv_obj_set_flex_flow(c.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Name
  c.name = lv_label_create(c.card);
  lv_label_set_text(c.name, t->name);
  lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(c.name, lv_pct(100));
  lv_obj_set_style_text_align(c.name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(c.name, UI_FONT_M, 0);
  lv_obj_set_style_text_color(c.name, lv_color_white(), 0);

  // Bar "well" - takes all remaining height
  c.well = plain(c.card);
  lv_obj_set_width(c.well, lv_pct(78));
  lv_obj_set_flex_grow(c.well, 1);
  lv_obj_set_style_bg_color(c.well, lv_color_hex(COL_WELL), 0);
  lv_obj_set_style_bg_opa(c.well, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c.well, 6, 0);
  lv_obj_set_style_clip_corner(c.well, true, 0);

  // Fill, grows from the bottom; height is a % of the well
  c.fill = plain(c.well);
  lv_obj_set_size(c.fill, lv_pct(100), 0);
  lv_obj_set_style_bg_opa(c.fill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(c.fill, fillColor(c.ft, UI_OK), 0);
  lv_obj_align(c.fill, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Segment dividers drawn on top of the fill
  for (int i = 1; i < SEGMENTS; i++) {
    lv_obj_t *d = plain(c.well);
    lv_obj_set_size(d, lv_pct(100), 2);
    lv_obj_set_style_bg_color(d, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_align(d, LV_ALIGN_TOP_MID);
    lv_obj_set_y(d, lv_pct(100 * i / SEGMENTS));
  }

  // "45 %"  - big number, smaller percent sign
  lv_obj_t *row = plain(c.card);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 3, 0);

  c.pct = lv_label_create(row);
  lv_label_set_text(c.pct, "--");
  lv_obj_set_style_text_font(c.pct, UI_FONT_XL, 0);

  c.unit = lv_label_create(row);
  lv_label_set_text(c.unit, "%");
  lv_obj_set_style_text_font(c.unit, UI_FONT_L, 0);
  lv_obj_set_style_pad_bottom(c.unit, 4, 0);

  // "8/18 L"
  c.litres = lv_label_create(c.card);
  lv_label_set_text(c.litres, "");
  lv_obj_set_style_text_font(c.litres, UI_FONT_S, 0);
  lv_obj_set_style_text_color(c.litres, lv_color_hex(0xB8C4D0), 0);
}

static void build() {
  lv_obj_clean(s_tab);
  s_count = 0;
  s_builtVer = n2kTankListVersion();
  int n = n2kTankCount();

  if (n == 0) {
    lv_obj_t *l = lv_label_create(s_tab);
    lv_label_set_text(l, "No tanks configured or seen on the bus");
    lv_obj_set_style_text_color(l, uiLevelColor(UI_STALE), 0);
    return;
  }

  // 1-3 tanks: roomy cards; 4: narrower; 5+: same width, row scrolls sideways
  lv_coord_t w = n <= 3 ? 136 : 104;

  for (int i = 0; i < n && i < N2K_MAX_TANKS; i++) {
    makeCard(s_cards[s_count], n2kTankAt(i), w);
    s_count++;
  }
}

static void tick(lv_timer_t *) {
  if (s_builtVer != n2kTankListVersion()) build();
  char buf[32];

  for (int i = 0; i < s_count; i++) {
    TankCard &c = s_cards[i];
    double pct, cap;
    bool has = n2kGet(Q_TANK_LEVEL, c.inst, c.ft, pct);

    if (has) {
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      UiLevel l = tankLevel(c.ft, c.inst, pct);
      lv_color_t txt = (l == UI_OK) ? lv_color_white() : uiLevelColor(l);

      snprintf(buf, sizeof(buf), "%.0f", pct);
      uiLabelSetIfChanged(c.pct, buf);
      lv_obj_set_style_text_color(c.pct, txt, 0);
      lv_obj_set_style_text_color(c.unit, txt, 0);

      lv_obj_set_height(c.fill, lv_pct((lv_coord_t)(pct + 0.5)));
      lv_obj_set_style_bg_color(c.fill, fillColor(c.ft, l), 0);
      lv_obj_set_style_border_color(c.card, l == UI_ALARM ? lv_palette_main(LV_PALETTE_RED)
                                                          : frameColor(c.ft), 0);

      if (n2kGet(Q_TANK_CAP, c.inst, c.ft, cap, 60000) && cap > 0)
        snprintf(buf, sizeof(buf), "%.0f/%.0f L", pct * cap / 100.0, cap);
      else
        buf[0] = 0;
      uiLabelSetIfChanged(c.litres, buf);
    } else {
      uiLabelSetIfChanged(c.pct, "--");
      lv_obj_set_style_text_color(c.pct, uiLevelColor(UI_STALE), 0);
      lv_obj_set_style_text_color(c.unit, uiLevelColor(UI_STALE), 0);
      lv_obj_set_height(c.fill, 0);
      lv_obj_set_style_border_color(c.card, frameColor(c.ft), 0);
      uiLabelSetIfChanged(c.litres, "");
    }
  }
}

void ui_tanks_create(lv_obj_t *tab) {
  s_tab = tab;
  uiPrepTab(tab);
  // Override the shared layout: one horizontal row of full-height cards
  lv_obj_set_style_bg_color(tab, lv_color_black(), 0);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(tab, LV_DIR_HOR);
  lv_obj_set_style_pad_column(tab, 10, 0);
  build();
  tick(nullptr);
  lv_timer_create(tick, 500, nullptr);
}
