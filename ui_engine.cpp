// ui_engine.cpp - Engine tab, analog gauge style
//
//  +--------------------------------------------+
//  | Babord  #0                  [preset  v]    |
//  | +--------+   .-''''''-.     +--------+     |
//  | |Oil temp|  /  RPM     \    | Hours  |     |
//  | +--------+ |  needle    |   +--------+     |
//  | |Fuel    |  \  1850    /   | Status |     |
//  | +--------+   '-......-'     +--------+     |
//  |  (Coolant)    (Oil press)    (Volts)       |
//  +--------------------------------------------+
//
// Shows ONE engine: the N2K instance in n2kSettings.engInstance.
// Twin engines = two displays, one on "Babord (0)", one on "Styrbord (1)".
// Gauge ranges and zone limits come from n2k_limits (editable in the N2K settings tab).
#include <stdio.h>
#include <string.h>
#include "ui_engine.h"
#include "ui_gauge.h"
#include "ui_n2k_common.h"
#include "n2k_config.h"
#include "n2k_data.h"
#include "n2k_settings.h"
#include "n2k_limits.h"

struct EnginePreset { uint8_t instance; const char *name; };
static const EnginePreset PRESETS[] = {
  { 0, "Motor"    },
  { 0, "Babord"   },
  { 1, "Styrbord" },
  { 2, "Motor 3"  },
  { 3, "Motor 4"  },
};
static const char *PRESET_OPTS =
  "Single engine (0)\nBabord / Port (0)\nStyrbord / Stbd (1)\nInstance 2\nInstance 3";
static const int PRESET_N = sizeof(PRESETS) / sizeof(PRESETS[0]);

// PGN 127489 discrete status bits
static const char *STATUS1_TXT[16] = {
  "CHECK ENGINE", "OVER TEMP", "LOW OIL PRESS", "LOW OIL LEVEL", "LOW FUEL PRESS",
  "LOW VOLTAGE", "LOW COOLANT", "WATER FLOW", "WATER IN FUEL", "CHARGE",
  "PREHEAT", "HIGH BOOST", "REV LIMIT", "EGR", "THROTTLE SENSOR", "EMERGENCY STOP"
};
static const char *STATUS2_TXT[8] = {
  "WARNING 1", "WARNING 2", "POWER REDUCED", "MAINTENANCE", "ENGINE COMM",
  "SUB THROTTLE", "NEUTRAL START", "SHUTTING DOWN"
};

static lv_obj_t *s_lblName, *s_dd;
static UiGauge s_rpm, s_cool, s_oilP, s_volt;
static UiTile  s_oilT, s_fuel, s_hours, s_status;
static uint32_t s_shownVer = 0;
static lv_obj_t *s_tab = nullptr;
static bool s_keepAwake = false;

static uint32_t s_limVer = 0;
static uint8_t  s_limInst = 255;
static N2kLimits L_RPM, L_COOL, L_OILP, L_VOLT, L_OILT, L_FUEL;

bool ui_engine_keep_awake() { return s_keepAwake; }

// Limits are per engine instance; reloaded when edited or the engine selection changes
static void reloadLimits(uint8_t e) {
  L_RPM  = n2kLimitsGet(Q_ENG_RPM,       e, 0);
  L_COOL = n2kLimitsGet(Q_ENG_COOL_T,    e, 0);
  L_OILP = n2kLimitsGet(Q_ENG_OIL_P,     e, 0);
  L_VOLT = n2kLimitsGet(Q_ENG_ALT_V,     e, 0);
  L_OILT = n2kLimitsGet(Q_ENG_OIL_T,     e, 0);
  L_FUEL = n2kLimitsGet(Q_ENG_FUEL_RATE, e, 0);
}

static int presetIndexFor(uint8_t inst, const char *name) {
  for (int i = 0; i < PRESET_N; i++)
    if (PRESETS[i].instance == inst && strcmp(PRESETS[i].name, name) == 0) return i;
  for (int i = 0; i < PRESET_N; i++)
    if (PRESETS[i].instance == inst) return i;
  return 0;
}

static void onPreset(lv_event_t *) {
  uint16_t i = lv_dropdown_get_selected(s_dd);
  if (i < PRESET_N) n2kSetEngine(PRESETS[i].instance, PRESETS[i].name, n2kSettings.engSource);
}

static lv_obj_t *plainBox(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  return b;
}

static lv_obj_t *column(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *c = plainBox(parent, w, h);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return c;
}

static void tick(lv_timer_t *) {
  char buf[48];
  const uint8_t e = n2kSettings.engInstance;

  if (s_shownVer != n2kSettingsVersion()) {
    s_shownVer = n2kSettingsVersion();
    snprintf(buf, sizeof(buf), "%s  #%u", n2kSettings.engName, e);
    lv_label_set_text(s_lblName, buf);
    lv_dropdown_set_selected(s_dd, presetIndexFor(e, n2kSettings.engName));
  }

  if (s_limVer != n2kLimitsVersion() || s_limInst != e) {
    s_limVer = n2kLimitsVersion();
    s_limInst = e;
    reloadLimits(e);
    uiGaugeApplyLimits(s_rpm,  L_RPM);
    uiGaugeApplyLimits(s_cool, L_COOL);
    uiGaugeApplyLimits(s_oilP, L_OILP);
    uiGaugeApplyLimits(s_volt, L_VOLT);
  }

  double rpm = 0, v;
  bool hasRpm = n2kGet(Q_ENG_RPM, e, 0, rpm);
  bool running = hasRpm && rpm > ENG_RUNNING_RPM;

  // RPM: needle on the x100 scale, readout in full rpm rounded to 10
  if (hasRpm) {
    snprintf(buf, sizeof(buf), "%d", (int)(rpm + 5) / 10 * 10);
    uiGaugeSet(s_rpm, true, rpm, uiLevelFrom(n2kLimitsEval(L_RPM, rpm)), buf);
  } else uiGaugeSet(s_rpm, false, 0, UI_STALE);

  bool h = n2kGet(Q_ENG_COOL_T, e, 0, v);
  uiGaugeSet(s_cool, h, v, uiLevelFrom(n2kLimitsEval(L_COOL, v)));

  // Low oil pressure / low charge voltage only count while the engine runs
  h = n2kGet(Q_ENG_OIL_P, e, 0, v);
  uiGaugeSet(s_oilP, h, v, uiLevelFrom(n2kLimitsEval(L_OILP, v, running)));

  h = n2kGet(Q_ENG_ALT_V, e, 0, v);
  uiGaugeSet(s_volt, h, v, uiLevelFrom(n2kLimitsEval(L_VOLT, v, running)));

  if (n2kGet(Q_ENG_OIL_T, e, 0, v)) {
    snprintf(buf, sizeof(buf), "%.0f", v);
    uiTileSet(s_oilT, buf, uiLevelFrom(n2kLimitsEval(L_OILT, v)));
  } else uiTileSet(s_oilT, "--", UI_STALE);

  if (n2kGet(Q_ENG_FUEL_RATE, e, 0, v)) {
    snprintf(buf, sizeof(buf), "%.1f", v);
    uiTileSet(s_fuel, buf, uiLevelFrom(n2kLimitsEval(L_FUEL, v)));
  } else uiTileSet(s_fuel, "--", UI_STALE);

  if (n2kGet(Q_ENG_HOURS, e, 0, v, 60000)) {          // hours are sent slowly by some gateways
    snprintf(buf, sizeof(buf), "%.1f", v);
    uiTileSet(s_hours, buf, UI_OK);
  } else uiTileSet(s_hours, "--", UI_STALE);

  // Status tile: engine-reported faults first, otherwise RUN / STOP
  double st1 = 0, st2 = 0;
  bool h1 = n2kGet(Q_ENG_STATUS1, e, 0, st1), h2 = n2kGet(Q_ENG_STATUS2, e, 0, st2);
  uint16_t b1 = h1 ? (uint16_t)st1 : 0;
  uint8_t  b2 = h2 ? (uint8_t)st2 : 0;
  if (!running) b1 &= ~((1u << 9) | (1u << 10));    // charge/preheat lamps are normal at key-on
  const char *fault = nullptr;
  for (int i = 0; i < 16 && !fault; i++) if (b1 & (1u << i)) fault = STATUS1_TXT[i];
  for (int i = 0; i < 8 && !fault; i++)  if (b2 & (1u << i)) fault = STATUS2_TXT[i];

  // ---- screensaver: keep the display on while engine data is shown.
  // lv_obj_is_visible() is false when another tab is selected (tab scrolled off screen)
  // or when a different screen (e.g. a screensaver screen) is loaded.
  bool shown = lv_obj_is_visible(s_tab);
#if ENG_KEEP_AWAKE == 1
  s_keepAwake = shown;
#elif ENG_KEEP_AWAKE == 2
  s_keepAwake = shown && running;
#else
  s_keepAwake = false;
#endif
  if (s_keepAwake) lv_disp_trig_activity(nullptr);   // resets lv_disp_get_inactive_time()

  if (fault) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "\n%s", fault);
    uiTileSet(s_status, buf, UI_ALARM);
  } else if (running) {
    uiTileSet(s_status, LV_SYMBOL_OK " RUN", UI_OK);
    lv_obj_set_style_text_color(s_status.value, lv_palette_main(LV_PALETTE_GREEN), 0);
  } else if (hasRpm || h1) {
    uiTileSet(s_status, "STOP", UI_OK);
  } else {
    uiTileSet(s_status, "--", UI_STALE);
  }
}

void ui_engine_create(lv_obj_t *tab) {
  s_tab = tab;
  uiPrepTab(tab);
  lv_obj_set_style_bg_color(tab, lv_color_black(), 0);
  lv_obj_set_style_pad_row(tab, 6, 0);

  reloadLimits(n2kSettings.engInstance);
  s_limInst = n2kSettings.engInstance;
  s_limVer  = n2kLimitsVersion();

  // ---- header: engine name + preset selector
  lv_obj_t *hdr = plainBox(tab, lv_pct(100), 40);
  s_lblName = lv_label_create(hdr);
  lv_obj_set_style_text_font(s_lblName, UI_FONT_L, 0);
  lv_obj_set_style_text_color(s_lblName, lv_color_white(), 0);
  lv_obj_align(s_lblName, LV_ALIGN_LEFT_MID, 4, 0);

  s_dd = lv_dropdown_create(hdr);
  lv_dropdown_set_options(s_dd, PRESET_OPTS);
  lv_obj_set_width(s_dd, 200);
  lv_obj_align(s_dd, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(s_dd, onPreset, LV_EVENT_VALUE_CHANGED, nullptr);

  // ---- middle row: side tiles + big tachometer
  const lv_coord_t rowH = 222, sideW = 104, tileH = 106;
  lv_obj_t *row = plainBox(tab, lv_pct(100), rowH);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *left = column(row, sideW, rowH);
  s_oilT = uiTile(left, "Oil temp", "\xC2\xB0""C", UI_FONT_L, sideW, tileH);
  s_fuel = uiTile(left, "Fuel",     "L/h",          UI_FONT_L, sideW, tileH);

  const UiGaugeCfg rpmCfg = { "RPM x100", 0.01f, 240, rowH, UI_FONT_XL, UI_FONT_M, 0 };
  s_rpm = uiGauge(row, rpmCfg, L_RPM);

  lv_obj_t *right = column(row, sideW, rowH);
  s_hours  = uiTile(right, "Hours",  "h", UI_FONT_M, sideW, tileH);
  s_status = uiTile(right, "Engine", "",  UI_FONT_M, sideW, tileH);
  lv_label_set_long_mode(s_status.value, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_status.value, lv_pct(100));
  lv_obj_set_style_text_align(s_status.value, LV_TEXT_ALIGN_CENTER, 0);

  // ---- bottom row: three small gauges
  const lv_coord_t gs = 140;
  lv_obj_t *bottom = plainBox(tab, lv_pct(100), gs);
  lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const UiGaugeCfg coolCfg = { "Coolant \xC2\xB0""C", 1, 240, gs, UI_FONT_M, UI_FONT_S, 0 };
  const UiGaugeCfg oilCfg  = { "Oil bar",             1, 240, gs, UI_FONT_M, UI_FONT_S, 1 };
  const UiGaugeCfg voltCfg = { "Alt V",               1, 240, gs, UI_FONT_M, UI_FONT_S, 1 };
  s_cool = uiGauge(bottom, coolCfg, L_COOL);
  s_oilP = uiGauge(bottom, oilCfg,  L_OILP);
  s_volt = uiGauge(bottom, voltCfg, L_VOLT);

  s_shownVer = 0;
  tick(nullptr);
  lv_timer_create(tick, 250, nullptr);
}
