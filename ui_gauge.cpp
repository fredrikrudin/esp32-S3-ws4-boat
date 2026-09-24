// ui_gauge.cpp
#include <math.h>
#include <stdio.h>
#include "ui_gauge.h"

static const uint32_t COL_FACE  = 0x101B28;
static const uint32_t COL_RIM   = 0x2A3D55;
static const uint32_t COL_MINOR = 0x6F8399;
static const int      MUL       = 100;      // internal integer resolution (0.01 scale units)

UiLevel uiLevelFrom(N2kLevel l) {
  return l == N2K_LVL_ALARM ? UI_ALARM : l == N2K_LVL_WARN ? UI_WARN : UI_OK;
}

// Tick labels: internal integer -> scale units. user_data holds the label decimals.
static void tickLabelCb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *d = lv_event_get_draw_part_dsc(e);
  if (!d || d->class_p != &lv_meter_class || d->type != LV_METER_DRAW_PART_TICK || !d->text) return;
  int dec = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
  lv_snprintf(d->text, d->text_length, "%.*f", dec, d->value / (float)MUL);
}

// Pick a "nice" major step (1/2/5 x 10^n) giving 2..5 major intervals
static void autoTicks(float span, uint16_t &ticks, uint16_t &nth, int &labelDec) {
  float base = powf(10.0f, floorf(log10f(span / 4.0f)));
  static const float MULTS[] = { 1, 2, 5, 10 };
  float major = base * 10, m = 10;
  for (float k : MULTS) if (span / (base * k) <= 5.0f) { major = base * k; m = k; break; }
  nth = (m == 2) ? 4 : 5;
  float minor = major / nth;
  int t = (int)lroundf(span / minor) + 1;
  if (t > 61) t = 61;
  if (t < 2) t = 2;
  ticks = (uint16_t)t;
  labelDec = major >= 1.0f ? 0 : (major >= 0.1f ? 1 : 2);
}

void uiGaugeApplyLimits(UiGauge &g, const N2kLimits &l) {
  float mn = l.gMin * g.factor, mx = l.gMax * g.factor;
  if (!(mx > mn)) { mn = 0; mx = 1; }
  g.lo = lroundf(mn * MUL);
  g.hi = lroundf(mx * MUL);

  uint16_t ticks, nth; int dec;
  autoTicks(mx - mn, ticks, nth, dec);
  lv_obj_set_user_data(g.meter, (void *)(intptr_t)dec);
  lv_meter_set_scale_ticks(g.meter, g.scale, ticks, 2, g.size / 20, lv_color_hex(COL_MINOR));
  lv_meter_set_scale_major_ticks(g.meter, g.scale, nth, 3, g.size / 12, lv_color_white(), g.size / 18);
  lv_meter_set_scale_range(g.meter, g.scale, g.lo, g.hi, g.sweep, 90 + (360 - g.sweep) / 2);

  // Zones: red | amber | green | amber | red, from whichever limits are set
  struct Seg { float a, b; uint32_t c; } seg[5];
  int n = 0;
  bool any = !isnan(l.alarmLo) || !isnan(l.warnLo) || !isnan(l.warnHi) || !isnan(l.alarmHi);
  if (any) {
    float cur = l.gMin;
    if (!isnan(l.alarmLo)) { seg[n++] = { l.gMin, l.alarmLo, ZONE_RED };   cur = l.alarmLo; }
    if (!isnan(l.warnLo))  { seg[n++] = { cur,    l.warnLo,  ZONE_AMBER }; cur = l.warnLo; }
    float hiStart = !isnan(l.warnHi) ? l.warnHi : !isnan(l.alarmHi) ? l.alarmHi : l.gMax;
    seg[n++] = { cur, hiStart, ZONE_GREEN };
    if (!isnan(l.warnHi))  seg[n++] = { l.warnHi, !isnan(l.alarmHi) ? l.alarmHi : l.gMax, ZONE_AMBER };
    if (!isnan(l.alarmHi)) seg[n++] = { l.alarmHi, l.gMax, ZONE_RED };
  }
  for (int i = 0; i < 5; i++) {
    lv_meter_indicator_t *z = g.zone[i];
    if (i < n) {
      int32_t a = lroundf(seg[i].a * g.factor * MUL), b = lroundf(seg[i].b * g.factor * MUL);
      a = a < g.lo ? g.lo : a > g.hi ? g.hi : a;
      b = b < g.lo ? g.lo : b > g.hi ? g.hi : b;
      z->type_data.arc.color = lv_color_hex(seg[i].c);
      z->opa = (b > a) ? LV_OPA_COVER : LV_OPA_TRANSP;
      lv_meter_set_indicator_start_value(g.meter, z, a);
      lv_meter_set_indicator_end_value(g.meter, z, b > a ? b : a);
    } else {
      z->opa = LV_OPA_TRANSP;
      lv_meter_set_indicator_start_value(g.meter, z, g.lo);
      lv_meter_set_indicator_end_value(g.meter, z, g.lo);
    }
  }
  lv_meter_set_indicator_value(g.meter, g.needle, g.lo);
  lv_obj_invalidate(g.meter);
}

UiGauge uiGauge(lv_obj_t *parent, const UiGaugeCfg &c, const N2kLimits &lim) {
  UiGauge g;
  g.factor = c.factor > 0 ? c.factor : 1.0f;
  g.sweep = c.sweep;
  g.size = c.size;
  g.decimals = c.decimals;

  g.meter = lv_meter_create(parent);
  lv_obj_set_size(g.meter, c.size, c.size);
  lv_obj_clear_flag(g.meter, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g.meter, lv_color_hex(COL_FACE), 0);
  lv_obj_set_style_border_width(g.meter, 2, 0);
  lv_obj_set_style_border_color(g.meter, lv_color_hex(COL_RIM), 0);
  lv_obj_set_style_pad_all(g.meter, c.size / 18, 0);
  lv_obj_set_style_text_color(g.meter, lv_color_hex(0xD0DAE4), LV_PART_TICKS);
  lv_obj_set_style_text_font(g.meter, c.tickFont, LV_PART_TICKS);
  lv_obj_set_style_bg_color(g.meter, lv_color_hex(0xE8E8E8), LV_PART_INDICATOR);   // pivot
  lv_obj_set_style_width(g.meter, c.size / 12, LV_PART_INDICATOR);
  lv_obj_set_style_height(g.meter, c.size / 12, LV_PART_INDICATOR);

  g.scale = lv_meter_add_scale(g.meter);
  lv_coord_t zw = c.size / 26 + 2;
  for (int i = 0; i < 5; i++) g.zone[i] = lv_meter_add_arc(g.meter, g.scale, zw, lv_color_hex(ZONE_GREEN), 0);
  g.needle = lv_meter_add_needle_line(g.meter, g.scale, c.size >= 180 ? 5 : 3,
                                      lv_palette_main(LV_PALETTE_ORANGE), -(c.size / 12));
  lv_obj_add_event_cb(g.meter, tickLabelCb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  g.title = lv_label_create(g.meter);
  lv_label_set_text(g.title, c.title);
  lv_obj_set_style_text_font(g.title, UI_FONT_S, 0);
  lv_obj_set_style_text_color(g.title, lv_color_hex(0x8FA3B8), 0);
  lv_obj_align(g.title, LV_ALIGN_CENTER, 0, c.size * 41 / 100);      // under the value

  g.value = lv_label_create(g.meter);
  lv_label_set_text(g.value, "--");
  lv_obj_set_style_text_font(g.value, c.valueFont, 0);
  lv_obj_set_style_text_color(g.value, uiLevelColor(UI_STALE), 0);
  lv_obj_align(g.value, LV_ALIGN_CENTER, 0, c.size * 27 / 100);      // in the open bottom sector

  uiGaugeApplyLimits(g, lim);
  return g;
}

void uiGaugeSet(UiGauge &g, bool valid, float v, UiLevel level, const char *text) {
  if (!valid) {
    lv_meter_set_indicator_value(g.meter, g.needle, g.lo);
    uiLabelSetIfChanged(g.value, "--");
    lv_obj_set_style_text_color(g.value, uiLevelColor(UI_STALE), 0);
    lv_obj_set_style_border_color(g.meter, lv_color_hex(COL_RIM), 0);
    return;
  }
  int32_t iv = lroundf(v * g.factor * MUL);
  if (iv < g.lo) iv = g.lo;
  if (iv > g.hi) iv = g.hi;
  lv_meter_set_indicator_value(g.meter, g.needle, iv);

  char buf[24];
  if (!text) { snprintf(buf, sizeof(buf), "%.*f", g.decimals, v); text = buf; }
  uiLabelSetIfChanged(g.value, text);
  lv_obj_set_style_text_color(g.value, uiLevelColor(level), 0);
  lv_obj_set_style_border_color(g.meter, level == UI_ALARM ? lv_palette_main(LV_PALETTE_RED)
                                        : level == UI_WARN ? lv_palette_main(LV_PALETTE_ORANGE)
                                        : lv_color_hex(COL_RIM), 0);
}
