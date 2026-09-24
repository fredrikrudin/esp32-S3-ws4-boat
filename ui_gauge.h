// ui_gauge.h - generic analog gauge (needle + tick scale + coloured zones + digital readout)
// Range, ticks and zones come from N2kLimits and can be changed at runtime.
// Built on lv_meter (LVGL 8.x, needs LV_USE_METER 1 in lv_conf.h - default on).
#pragma once
#include <lvgl.h>
#include "ui_n2k_common.h"
#include "n2k_limits.h"

#if LVGL_VERSION_MAJOR != 8
#error "ui_gauge uses lv_meter, which is LVGL 8.x only (LVGL 9 replaced it with lv_scale)"
#endif

#define ZONE_GREEN 0x2E9D4F
#define ZONE_AMBER 0xE0A020
#define ZONE_RED   0xD03030

struct UiGaugeCfg {
  const char      *title;       // e.g. "Coolant \xC2\xB0""C"
  float            factor;      // value -> scale units, e.g. 0.01 for an RPM x100 scale
  uint16_t         sweep;       // degrees; 240 leaves room for the digital value at the bottom
  lv_coord_t       size;        // square size in px
  const lv_font_t *valueFont;
  const lv_font_t *tickFont;
  uint8_t          decimals;    // digital readout decimals
};

struct UiGauge {
  lv_obj_t *meter, *title, *value;
  lv_meter_scale_t *scale;
  lv_meter_indicator_t *needle;
  lv_meter_indicator_t *zone[5];
  int32_t lo, hi;
  float factor;
  uint16_t sweep;
  lv_coord_t size;
  uint8_t decimals;
};

UiGauge  uiGauge(lv_obj_t *parent, const UiGaugeCfg &cfg, const N2kLimits &lim);
void     uiGaugeApplyLimits(UiGauge &g, const N2kLimits &lim);   // new range, ticks and zones
void     uiGaugeSet(UiGauge &g, bool valid, float value, UiLevel level, const char *text = nullptr);
UiLevel  uiLevelFrom(N2kLevel l);
