// ui_nav.cpp - Navigation tab
#include <stdio.h>
#include <math.h>
#include "ui_nav.h"
#include "ui_n2k_common.h"
#include "n2k_config.h"
#include "n2k_data.h"
#include "n2k_bus.h"
#include "n2k_limits.h"
#include "ui_gauge.h"

static UiTile s_depth, s_stw, s_sog, s_cog, s_sea, s_batt, s_pos;
static lv_obj_t *s_busDot;

static void fmtCoord(char *b, size_t n, double v, bool isLat) {
  char h = isLat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
  v = fabs(v);
  int d = (int)v;
  double m = (v - d) * 60.0;
  snprintf(b, n, isLat ? "%02d\xC2\xB0%06.3f'%c" : "%03d\xC2\xB0%06.3f'%c", d, m, h);
}

static void tick(lv_timer_t *) {
  char buf[64];
  double v, off;

  // Depth: below transducer + offset if the sounder is configured with one
  if (n2kGet(Q_DEPTH, 0, 0, v)) {
    bool hasOff = n2kGet(Q_DEPTH_OFFSET, 0, 0, off, 30000);
    double d = hasOff ? v + off : v;
    snprintf(buf, sizeof(buf), d < 10 ? "%.1f" : "%.0f", d);
    uiTileSet(s_depth, buf, uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_DEPTH, 0, 0), d)));
    uiLabelSetIfChanged(s_depth.title, !hasOff ? "Depth (below transducer)"
                                       : off >= 0 ? "Depth (below surface)" : "Depth (below keel)");
  } else uiTileSet(s_depth, "--", UI_STALE);

  if (n2kGet(Q_STW, 0, 0, v)) { snprintf(buf, sizeof(buf), "%.1f", v); uiTileSet(s_stw, buf, UI_OK); }
  else uiTileSet(s_stw, "--", UI_STALE);

  if (n2kGet(Q_SOG, 0, 0, v)) { snprintf(buf, sizeof(buf), "%.1f", v); uiTileSet(s_sog, buf, UI_OK); }
  else uiTileSet(s_sog, "--", UI_STALE);

  if (n2kGet(Q_COG, 0, 0, v)) { snprintf(buf, sizeof(buf), "%03.0f", v); uiTileSet(s_cog, buf, UI_OK); }
  else uiTileSet(s_cog, "--", UI_STALE);

  if (n2kGetAny(Q_SEA_TEMP, v)) { snprintf(buf, sizeof(buf), "%.1f", v); uiTileSet(s_sea, buf, UI_OK); }
  else uiTileSet(s_sea, "--", UI_STALE);

  uint8_t bi = NAV_BATT_INSTANCE;
  bool hb = (NAV_BATT_INSTANCE == 255) ? n2kGetAny(Q_BATT_V, v, N2K_STALE_MS, &bi) : n2kGet(Q_BATT_V, bi, 0, v);
  if (hb) {
    snprintf(buf, sizeof(buf), "%.2f", v);
    uiTileSet(s_batt, buf, uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_BATT_V, bi, 0), v)));
  }
  else uiTileSet(s_batt, "--", UI_STALE);

  double lat, lon;
  if (n2kGet(Q_LAT, 0, 0, lat) && n2kGet(Q_LON, 0, 0, lon)) {
    char a[24], b[24];
    fmtCoord(a, sizeof(a), lat, true);
    fmtCoord(b, sizeof(b), lon, false);
    snprintf(buf, sizeof(buf), "%s   %s", a, b);
    uiTileSet(s_pos, buf, UI_OK);
  } else uiTileSet(s_pos, "--", UI_STALE);

  lv_obj_set_style_bg_color(s_busDot, n2kBusOk() ? lv_palette_main(LV_PALETTE_GREEN)
                                                 : lv_palette_main(LV_PALETTE_RED), 0);
}

void ui_nav_create(lv_obj_t *tab) {
  uiPrepTab(tab);

  s_depth = uiTile(tab, "Depth", "m", UI_FONT_XL, lv_pct(100), 120);
  // small N2K bus status dot in the depth tile
  s_busDot = lv_obj_create(s_depth.box);
  lv_obj_set_size(s_busDot, 12, 12);
  lv_obj_set_style_radius(s_busDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_busDot, 0, 0);
  lv_obj_align(s_busDot, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  const lv_coord_t w = lv_pct(31), h = 84;
  s_stw  = uiTile(tab, "Speed (log)", "kn",            UI_FONT_L, w, h);
  s_sog  = uiTile(tab, "SOG",         "kn",            UI_FONT_L, w, h);
  s_cog  = uiTile(tab, "COG",         "\xC2\xB0""T",  UI_FONT_L, w, h);
  s_sea  = uiTile(tab, "Sea temp",    "\xC2\xB0""C",  UI_FONT_L, lv_pct(48), h);
  s_batt = uiTile(tab, "Battery",     "V",             UI_FONT_L, lv_pct(48), h);
  s_pos  = uiTile(tab, "Position",    "",              UI_FONT_M, lv_pct(100), 70);

  tick(nullptr);
  lv_timer_create(tick, 250, nullptr);
}
