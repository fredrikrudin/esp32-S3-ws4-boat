/* Start page (⌂ tab) for the boat:
 *
 *   +------------------------------------------------+
 *   | 14:32 Thu 24 Sep                         N2K ● |  <- becomes the alarm banner
 *   | DEPTH  4.8 m               | SPEED  5.6 kn      |
 *   | (RPM gauge) (Coolant gauge) | ENGINE Babord RUN |   one engine
 *   | (Port RPM)  (Starboard RPM) | ENGINES both      |   twin engines (Settings -> NMEA 2000)
 *   | BATTERY 87% | VOLTS 13.32  | TANKS  ▮ ▮ ▮      |
 *   +------------------------------------------------+
 *
 * Warnings and alarms: the header turns into a banner with the most important
 * message (red and blinking for alarms, orange for warnings) and the value's card
 * is outlined in the same colour. A new alarm wakes the screen saver and brings
 * this page up (unless the Engine tab is shown). Tap the banner to acknowledge:
 * it stops blinking but stays until the value is back in range.
 * Limits come from n2k_limits (Settings -> NMEA 2000), battery SOC from START_SOC_*.
 */
#include "app.h"
#include <ctype.h>
#include <stdarg.h>
#include "ui_gauge.h"
#include "ui_n2k_common.h"
#include "n2k_config.h"
#include "n2k_data.h"
#include "n2k_limits.h"
#include "n2k_settings.h"
#include "n2k_bus.h"

#define START_SOC_WARN 20   // battery state of charge warning below this %
#define START_SOC_ALARM 10  // ... alarm below this %
#define START_TANKS 4       // tanks shown on the start page (the Tanks tab shows all)

#define COL_CARD 0x16263A
#define COL_TITLE 0x8FA3B8
#define COL_BANNER_ALARM 0xC62828
#define COL_BANNER_BLINK 0x6A0F0F
#define COL_BANNER_WARN 0xE65100

/* layout inside the 480 x 430 tab */
#define PAD 4                 // padding
#define GAP 4                 // gap between cards
#define WUSE (480 - 2 * PAD)  // usable width
#define HDR_H 34
#define TOP_H 116
#define MID_H 148
#define BOT_H 112

/* ------------------------------------------------------------------ widgets */
static lv_obj_t *hdr, *lbl_time, *lbl_date, *dot_n2k;
static lv_obj_t *banner, *lbl_banner, *lbl_banner_more;
static lv_obj_t *c_depth, *v_depth, *u_depth, *t_depth_ref;
static lv_obj_t *c_speed, *v_speed, *u_speed, *t_speed_ref, *l_speed_sub;
static UiGauge g_rpm, g_cool, g_rpm2;   // twin: g_rpm = port, g_rpm2 = starboard, g_cool hidden
static lv_obj_t *c_eng, *t_eng, *box_single, *box_twin;
static lv_obj_t *l_eng_name, *l_eng_status, *l_eng_oil, *l_eng_alt, *l_eng_hours;   // one engine
static lv_obj_t *tw_name[2], *tw_status[2], *tw_vals[2];                           // twin engines
static int shown_twin = -1;           // layout currently shown (-1 = not yet)
static uint32_t shown_names_ver = 0;
static lv_obj_t *c_batt, *v_soc, *bar_soc, *l_charge;
static lv_obj_t *c_volt, *v_volt, *l_amp, *l_watt;
static lv_obj_t *c_tanks, *lbl_no_tanks;
static lv_obj_t *tk_bar[START_TANKS], *tk_pct[START_TANKS], *tk_name[START_TANKS];
static int tk_count = -1;
static uint32_t tk_ver = 0;
static uint32_t lim_ver = 0;
static uint8_t lim_inst = 255;
static int lim_twin = -1;

static lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(c, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_border_color(c, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  if (title) {
    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, UI_FONT_S, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 10, 7);
  }
  return c;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *f, uint32_t col, const char *txt = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_label_set_text(l, txt);
  return l;
}

/* big value with a smaller unit after it, sharing a baseline, centred in the card */
static lv_obj_t *mk_value_row(lv_obj_t *card, int y_ofs, const lv_font_t *vf, const lv_font_t *uf,
                              const char *unit, lv_obj_t **value, lv_obj_t **unit_lbl) {
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, y_ofs);
  *value = mk_label(row, vf, 0xFFFFFF, "--");
  *unit_lbl = mk_label(row, uf, 0xFFFFFF, unit);
  lv_obj_set_style_pad_bottom(*unit_lbl, lv_font_get_line_height(vf) / 8, 0);
  return row;
}

static void card_level(lv_obj_t *card, UiLevel l) {
  if (l == UI_ALARM || l == UI_WARN) {
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, uiLevelColor(l), 0);
  } else {
    lv_obj_set_style_border_width(card, 0, 0);
  }
}

static void value_color(lv_obj_t *v, lv_obj_t *u, UiLevel l) {
  lv_color_t c = (l == UI_OK) ? lv_color_white() : uiLevelColor(l);
  lv_obj_set_style_text_color(v, c, 0);
  if (u) lv_obj_set_style_text_color(u, c, 0);
}

/* ------------------------------------------------------------------ alerts */
struct StartAlert {
  UiLevel lvl;
  char text[40];
};
static StartAlert alerts[12];
static int n_alerts;
static char ack_text[40] = "";     // acknowledged top message
static char last_alarm[40] = "";   // last top alarm, to spot a new one
static bool blink_on = false;

static void alert(UiLevel l, const char *fmt, ...) {
  if (l != UI_ALARM && l != UI_WARN) return;
  if (n_alerts >= (int)(sizeof(alerts) / sizeof(alerts[0]))) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(alerts[n_alerts].text, sizeof(alerts[n_alerts].text), fmt, ap);
  va_end(ap);
  alerts[n_alerts].lvl = l;
  n_alerts++;
}

static void banner_cb(lv_event_t *e) {
  if (n_alerts) strlcpy(ack_text, lv_label_get_text(lbl_banner), sizeof(ack_text));
}

static const char *ENG_FAULT1[16] = {
  "CHECK ENGINE", "ENGINE OVER TEMP", "LOW OIL PRESSURE", "LOW OIL LEVEL", "LOW FUEL PRESSURE",
  "LOW SYSTEM VOLTAGE", "LOW COOLANT", "WATER FLOW", "WATER IN FUEL", "CHARGE WARNING",
  "PREHEAT", "HIGH BOOST", "REV LIMIT", "EGR SYSTEM", "THROTTLE SENSOR", "EMERGENCY STOP"
};
static const char *ENG_FAULT2[8] = {
  "ENGINE WARNING 1", "ENGINE WARNING 2", "POWER REDUCED", "MAINTENANCE NEEDED", "ENGINE COMM ERROR",
  "SUB THROTTLE", "NEUTRAL START", "ENGINE SHUTTING DOWN"
};

/* ------------------------------------------------------------------ engines */
struct EngView {
  bool has_rpm, running, has_cool, has_oil, has_alt, has_hours;
  double rpm, cool, oil, alt, hours;
  UiLevel rpm_lvl, cool_lvl, oil_lvl, alt_lvl, card_lvl;
  const char *fault;
};

/* Reads one engine, raises its alerts ("PORT COOLANT 101 C" when named) */
static void eval_engine(uint8_t e, const char *name, EngView &v) {
  memset(&v, 0, sizeof(v));
  char pre[20] = "";
  if (name) {
    snprintf(pre, sizeof(pre), "%s ", name);
    for (char *p = pre; *p; p++) *p = toupper(*p);
  }

  v.has_rpm = n2kGet(Q_ENG_RPM, e, 0, v.rpm);
  v.running = v.has_rpm && v.rpm > ENG_RUNNING_RPM;

  double st1 = 0, st2 = 0;
  bool h1 = n2kGet(Q_ENG_STATUS1, e, 0, st1), h2 = n2kGet(Q_ENG_STATUS2, e, 0, st2);
  uint16_t b1 = h1 ? (uint16_t)st1 : 0;
  uint8_t b2 = h2 ? (uint8_t)st2 : 0;
  if (!v.running) b1 &= ~((1u << 9) | (1u << 10));  // charge / preheat lamps are normal at key-on
  for (int i = 0; i < 16 && !v.fault; i++) if (b1 & (1u << i)) v.fault = ENG_FAULT1[i];
  for (int i = 0; i < 8 && !v.fault; i++) if (b2 & (1u << i)) v.fault = ENG_FAULT2[i];
  if (v.fault) alert(UI_ALARM, "%s%s", pre, v.fault);

  if (v.has_rpm) {
    v.rpm_lvl = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_ENG_RPM, e, 0), v.rpm));
    alert(v.rpm_lvl, "%sRPM %d", pre, (int)v.rpm);
  }
  if ((v.has_cool = n2kGet(Q_ENG_COOL_T, e, 0, v.cool))) {
    v.cool_lvl = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_ENG_COOL_T, e, 0), v.cool));
    alert(v.cool_lvl, "%sCOOLANT %.0f \xC2\xB0""C", pre, v.cool);
  }
  if ((v.has_oil = n2kGet(Q_ENG_OIL_P, e, 0, v.oil))) {
    v.oil_lvl = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_ENG_OIL_P, e, 0), v.oil, v.running));
    alert(v.oil_lvl, "%sOIL PRESSURE %.1f bar", pre, v.oil);
  }
  if ((v.has_alt = n2kGet(Q_ENG_ALT_V, e, 0, v.alt))) {
    v.alt_lvl = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_ENG_ALT_V, e, 0), v.alt, v.running));
    alert(v.alt_lvl, "%sALTERNATOR %.1f V", pre, v.alt);
  }
  v.has_hours = n2kGet(Q_ENG_HOURS, e, 0, v.hours, 60000);

  /* the engine card's outline: oil and alternator (the gauges show RPM and coolant) */
  v.card_lvl = v.oil_lvl > v.alt_lvl ? v.oil_lvl : v.alt_lvl;
  if (name && v.cool_lvl > v.card_lvl) v.card_lvl = v.cool_lvl;   // twin: no coolant gauge
}

static void show_rpm(UiGauge &g, const EngView &v) {
  if (!v.has_rpm) {
    uiGaugeSet(g, false, 0, UI_STALE);
    return;
  }
  char b[12];
  snprintf(b, sizeof(b), "%d", (int)(v.rpm + 5) / 10 * 10);
  uiGaugeSet(g, true, v.rpm, v.rpm_lvl, b);
}

static void show_status(lv_obj_t *l, const EngView &v, bool big) {
  char b[48];
  if (v.fault) {
    snprintf(b, sizeof(b), LV_SYMBOL_WARNING " %s", big ? v.fault : "FAULT");
    set_label(l, b);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
  } else if (v.running) {
    set_label(l, LV_SYMBOL_OK " RUN");
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREEN), 0);
  } else {
    set_label(l, v.has_rpm ? "STOP" : "--");
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TITLE), 0);
  }
}

/* ------------------------------------------------------------------ build */
void build_home_tab() {
  lv_obj_t *t = tab_home;
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(t, 0, 0);
  lv_obj_set_style_bg_color(t, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);

  int y = PAD;

  /* ---- header: time, date, NMEA 2000 status */
  hdr = lv_obj_create(t);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_pos(hdr, PAD, y);
  lv_obj_set_size(hdr, WUSE, HDR_H);
  lbl_time = mk_label(hdr, UI_FONT_L, 0xFFFFFF, "--:--");
  lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 4, 0);
  lbl_date = mk_label(hdr, UI_FONT_M, COL_TITLE);
  lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -2);
  dot_n2k = lv_obj_create(hdr);
  lv_obj_remove_style_all(dot_n2k);
  lv_obj_set_size(dot_n2k, 10, 10);
  lv_obj_set_style_radius(dot_n2k, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot_n2k, LV_OPA_COVER, 0);
  lv_obj_align(dot_n2k, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_t *n2k_txt = mk_label(hdr, UI_FONT_S, COL_TITLE, "N2K");
  lv_obj_align(n2k_txt, LV_ALIGN_RIGHT_MID, -20, 0);

  /* ---- alarm banner, same place as the header */
  banner = lv_obj_create(t);
  lv_obj_set_pos(banner, PAD, y);
  lv_obj_set_size(banner, WUSE, HDR_H);
  lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(banner, 8, 0);
  lv_obj_set_style_border_width(banner, 0, 0);
  lv_obj_set_style_pad_hor(banner, 12, 0);
  lv_obj_set_style_pad_ver(banner, 0, 0);
  lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(banner, banner_cb, LV_EVENT_CLICKED, NULL);
  lbl_banner = mk_label(banner, UI_FONT_M, 0xFFFFFF);
  lv_obj_align(lbl_banner, LV_ALIGN_LEFT_MID, 0, 0);
  lbl_banner_more = mk_label(banner, UI_FONT_S, 0xFFCDD2);
  lv_obj_align(lbl_banner_more, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
  y += HDR_H + GAP;

  /* ---- depth and speed */
  const int dw = WUSE * 57 / 100, sw = WUSE - dw - GAP;
  c_depth = mk_card(t, PAD, y, dw, TOP_H, "DEPTH");
  t_depth_ref = mk_label(c_depth, UI_FONT_S, COL_TITLE);
  lv_obj_align(t_depth_ref, LV_ALIGN_TOP_RIGHT, -10, 7);
  mk_value_row(c_depth, 10, UI_FONT_XL, UI_FONT_L, "m", &v_depth, &u_depth);

  c_speed = mk_card(t, PAD + dw + GAP, y, sw, TOP_H, "SPEED");
  t_speed_ref = mk_label(c_speed, UI_FONT_S, COL_TITLE);
  lv_obj_align(t_speed_ref, LV_ALIGN_TOP_RIGHT, -10, 7);
  mk_value_row(c_speed, 0, UI_FONT_XL, UI_FONT_M, "kn", &v_speed, &u_speed);
  l_speed_sub = mk_label(c_speed, UI_FONT_S, COL_TITLE);
  lv_obj_align(l_speed_sub, LV_ALIGN_BOTTOM_MID, 0, -8);
  y += TOP_H + GAP;

  /* ---- engine: RPM + coolant gauges (one engine) or port + starboard RPM (twin), + engine card */
  const UiGaugeCfg rpm_cfg = { "RPM x100", 0.01f, 240, MID_H, UI_FONT_L, UI_FONT_S, 0 };
  const UiGaugeCfg cool_cfg = { "Coolant \xC2\xB0""C", 1, 240, MID_H, UI_FONT_L, UI_FONT_S, 0 };
  lv_obj_t *gbox = lv_obj_create(t);   // flex row: hidden gauges drop out, so the order is
  lv_obj_remove_style_all(gbox);       // [rpm][coolant] or [port rpm][starboard rpm]
  lv_obj_set_pos(gbox, PAD, y);
  lv_obj_set_size(gbox, 2 * MID_H + GAP, MID_H);
  lv_obj_set_flex_flow(gbox, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(gbox, GAP, 0);
  g_rpm = uiGauge(gbox, rpm_cfg, n2kLimitsGet(Q_ENG_RPM, n2kSettings.engInstance, 0));
  g_cool = uiGauge(gbox, cool_cfg, n2kLimitsGet(Q_ENG_COOL_T, n2kSettings.engInstance, 0));
  g_rpm2 = uiGauge(gbox, rpm_cfg, n2kLimitsGet(Q_ENG_RPM, ENG_STBD_INSTANCE, 0));

  const int ex = PAD + 2 * (MID_H + GAP), ew = WUSE - 2 * (MID_H + GAP);
  c_eng = mk_card(t, ex, y, ew, MID_H, NULL);
  t_eng = mk_label(c_eng, UI_FONT_S, COL_TITLE, "ENGINE");
  lv_obj_set_pos(t_eng, 10, 7);

  /* one engine: name, status, oil / alternator / hours */
  box_single = lv_obj_create(c_eng);
  lv_obj_remove_style_all(box_single);
  lv_obj_set_size(box_single, ew, MID_H);
  l_eng_name = mk_label(box_single, UI_FONT_M, 0xFFFFFF);
  lv_obj_set_pos(l_eng_name, 10, 24);
  l_eng_status = mk_label(box_single, UI_FONT_M, 0xFFFFFF, "--");
  lv_obj_set_width(l_eng_status, ew - 8);
  lv_label_set_long_mode(l_eng_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(l_eng_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l_eng_status, LV_ALIGN_TOP_MID, 0, 56);
  const char *rows[3] = { "Oil", "Alt", "Hours" };
  lv_obj_t **vals[3] = { &l_eng_oil, &l_eng_alt, &l_eng_hours };
  for (int i = 0; i < 3; i++) {
    lv_obj_t *k = mk_label(box_single, UI_FONT_S, COL_TITLE, rows[i]);
    lv_obj_set_pos(k, 10, 88 + i * 19);
    *vals[i] = mk_label(box_single, UI_FONT_S, 0xFFFFFF, "--");
    lv_obj_align(*vals[i], LV_ALIGN_TOP_RIGHT, -10, 88 + i * 19);
  }

  /* twin engines: per engine a name + status line and a coolant / oil line */
  box_twin = lv_obj_create(c_eng);
  lv_obj_remove_style_all(box_twin);
  lv_obj_set_size(box_twin, ew, MID_H);
  for (int i = 0; i < 2; i++) {
    int yy = 28 + i * 58;
    tw_name[i] = mk_label(box_twin, UI_FONT_M, 0xFFFFFF);
    lv_obj_set_pos(tw_name[i], 10, yy);
    tw_status[i] = mk_label(box_twin, UI_FONT_S, 0xFFFFFF, "--");
    lv_obj_align(tw_status[i], LV_ALIGN_TOP_RIGHT, -10, yy + 3);
    tw_vals[i] = mk_label(box_twin, UI_FONT_S, 0xFFFFFF, "--");
    lv_obj_set_pos(tw_vals[i], 10, yy + 25);
  }
  lim_ver = n2kLimitsVersion();
  lim_inst = n2kSettings.engInstance;
  lim_twin = n2kSettings.twin;
  y += MID_H + GAP;

  /* ---- battery (SOC), volts, tanks */
  const int w1 = 150, w2 = 128, w3 = WUSE - w1 - w2 - 2 * GAP;
  c_batt = mk_card(t, PAD, y, w1, BOT_H, "BATTERY");
  l_charge = mk_label(c_batt, UI_FONT_S, 0xFFD54F);
  lv_obj_align(l_charge, LV_ALIGN_TOP_RIGHT, -10, 7);
  v_soc = mk_label(c_batt, UI_FONT_XL, 0xFFFFFF, "--");
  lv_obj_align(v_soc, LV_ALIGN_CENTER, 0, -4);
  bar_soc = lv_bar_create(c_batt);
  lv_obj_set_size(bar_soc, w1 - 24, 12);
  lv_obj_align(bar_soc, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_bar_set_range(bar_soc, 0, 100);
  lv_obj_set_style_bg_color(bar_soc, lv_color_hex(0x22354D), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar_soc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_soc, lv_color_hex(0x38A9F5), LV_PART_INDICATOR);

  c_volt = mk_card(t, PAD + w1 + GAP, y, w2, BOT_H, "VOLTS");
  v_volt = mk_label(c_volt, UI_FONT_L, 0xFFFFFF, "--");
  lv_obj_align(v_volt, LV_ALIGN_TOP_MID, 0, 32);
  l_amp = mk_label(c_volt, UI_FONT_M, 0xFFFFFF);
  lv_obj_align(l_amp, LV_ALIGN_TOP_MID, 0, 68);
  l_watt = mk_label(c_volt, UI_FONT_S, COL_TITLE);
  lv_obj_align(l_watt, LV_ALIGN_TOP_MID, 0, 90);

  c_tanks = mk_card(t, PAD + w1 + w2 + 2 * GAP, y, w3, BOT_H, "TANKS");
  lbl_no_tanks = mk_label(c_tanks, UI_FONT_S, COL_TITLE, "No tanks");
  lv_obj_center(lbl_no_tanks);

  home_timer_cb(NULL);
}

/* tank columns: rebuilt when a tank appears or is renamed */
static void build_tanks() {
  for (int i = 0; i < START_TANKS; i++) {
    if (tk_bar[i]) { lv_obj_del(tk_bar[i]); lv_obj_del(tk_pct[i]); lv_obj_del(tk_name[i]); }
    tk_bar[i] = tk_pct[i] = tk_name[i] = nullptr;
  }
  tk_ver = n2kTankListVersion();
  tk_count = n2kTankCount() < START_TANKS ? n2kTankCount() : START_TANKS;
  if (tk_count) lv_obj_add_flag(lbl_no_tanks, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(lbl_no_tanks, LV_OBJ_FLAG_HIDDEN);

  int w = lv_obj_get_width(c_tanks);
  int slot = tk_count ? (w - 16) / tk_count : 0;
  for (int i = 0; i < tk_count; i++) {
    N2kTank *tk = n2kTankAt(i);
    int cx = 8 + slot * i + slot / 2;
    tk_bar[i] = lv_bar_create(c_tanks);
    lv_obj_set_size(tk_bar[i], 20, 50);
    lv_obj_set_pos(tk_bar[i], cx - 10, 26);
    lv_bar_set_range(tk_bar[i], 0, 100);
    lv_obj_set_style_radius(tk_bar[i], 4, LV_PART_MAIN);
    lv_obj_set_style_radius(tk_bar[i], 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tk_bar[i], lv_color_hex(0x22354D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tk_bar[i], LV_OPA_COVER, LV_PART_MAIN);

    tk_pct[i] = mk_label(c_tanks, UI_FONT_S, 0xFFFFFF, "--");
    lv_obj_set_width(tk_pct[i], slot);
    lv_obj_set_style_text_align(tk_pct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tk_pct[i], cx - slot / 2, 80);

    char nm[12];                                   // first word: "Fresh water" -> "Fresh"
    strlcpy(nm, tk->name, sizeof(nm));
    char *sp = strchr(nm, ' ');
    if (sp) *sp = 0;
    tk_name[i] = mk_label(c_tanks, UI_FONT_S, COL_TITLE, nm);
    lv_obj_set_width(tk_name[i], slot);
    lv_label_set_long_mode(tk_name[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(tk_name[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tk_name[i], cx - slot / 2, 94);
  }
}

/* ------------------------------------------------------------------ update (every 500 ms) */
void home_timer_cb(lv_timer_t *t) {
  char b[64], tb[16], db[64];
  uint32_t now = millis();
  n_alerts = 0;
  blink_on = !blink_on;

  /* ---- time and date */
  format_local_time(tb, db);
  set_label(lbl_time, tb);
  set_label(lbl_date, db);
  lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -2);
  lv_obj_set_style_bg_color(dot_n2k, n2kBusOk() ? lv_palette_main(LV_PALETTE_GREEN)
                                                : lv_palette_main(LV_PALETTE_RED), 0);

  /* ---- engines first: their alarms matter most */
  const bool twin = n2kSettings.twin;
  if (shown_twin != (int)twin || shown_names_ver != n2kSettingsVersion()) {
    shown_twin = twin;
    shown_names_ver = n2kSettingsVersion();
    if (twin) {
      lv_obj_add_flag(g_cool.meter, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(g_rpm2.meter, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(box_single, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(box_twin, LV_OBJ_FLAG_HIDDEN);
      set_label(g_rpm.title, n2kSettings.portName);    // port on the left
      set_label(g_rpm2.title, n2kSettings.stbdName);   // starboard on the right
      set_label(tw_name[0], n2kSettings.portName);
      set_label(tw_name[1], n2kSettings.stbdName);
      set_label(t_eng, "ENGINES");
    } else {
      lv_obj_clear_flag(g_cool.meter, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_rpm2.meter, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(box_single, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(box_twin, LV_OBJ_FLAG_HIDDEN);
      set_label(g_rpm.title, "RPM x100");
      set_label(t_eng, "ENGINE");
    }
  }

  const uint8_t e = twin ? ENG_PORT_INSTANCE : n2kSettings.engInstance;
  if (lim_ver != n2kLimitsVersion() || lim_inst != e || lim_twin != (int)twin) {
    lim_ver = n2kLimitsVersion();
    lim_inst = e;
    lim_twin = twin;
    uiGaugeApplyLimits(g_rpm, n2kLimitsGet(Q_ENG_RPM, e, 0));
    uiGaugeApplyLimits(g_cool, n2kLimitsGet(Q_ENG_COOL_T, e, 0));
    uiGaugeApplyLimits(g_rpm2, n2kLimitsGet(Q_ENG_RPM, ENG_STBD_INSTANCE, 0));
  }

  EngView ev[2];
  int n_eng = twin ? 2 : 1;
  eval_engine(e, twin ? n2kSettings.portName : nullptr, ev[0]);
  if (twin) eval_engine(ENG_STBD_INSTANCE, n2kSettings.stbdName, ev[1]);

    show_rpm(g_rpm, ev[0]);
  if (twin) show_rpm(g_rpm2, ev[1]);
  else if (ev[0].has_cool) uiGaugeSet(g_cool, true, ev[0].cool, ev[0].cool_lvl);
  else uiGaugeSet(g_cool, false, 0, UI_STALE);

  bool any_fault = false;
  UiLevel eng_lvl = UI_OK;
  for (int i = 0; i < n_eng; i++) {
    any_fault |= ev[i].fault != nullptr;
    if (ev[i].card_lvl > eng_lvl) eng_lvl = ev[i].card_lvl;
  }

  if (!twin) {
    const EngView &a = ev[0];
    set_label(l_eng_name, n2kSettings.engName);
    show_status(l_eng_status, a, true);
    if (a.has_oil) { snprintf(b, sizeof(b), "%.1f bar", a.oil); set_label(l_eng_oil, b); }
    else set_label(l_eng_oil, "--");
    value_color(l_eng_oil, NULL, a.has_oil ? a.oil_lvl : UI_OK);
    if (a.has_alt) { snprintf(b, sizeof(b), "%.1f V", a.alt); set_label(l_eng_alt, b); }
    else set_label(l_eng_alt, "--");
    value_color(l_eng_alt, NULL, a.has_alt ? a.alt_lvl : UI_OK);
    if (a.has_hours) { snprintf(b, sizeof(b), "%.1f", a.hours); set_label(l_eng_hours, b); }
    else set_label(l_eng_hours, "--");
  } else {
    for (int i = 0; i < 2; i++) {
      const EngView &a = ev[i];
      show_status(tw_status[i], a, false);
      /* coolant (the gauge is gone in twin mode) and oil pressure */
      char c1[16] = "--", c2[16] = "--";
      if (a.has_cool) snprintf(c1, sizeof(c1), "%.0f\xC2\xB0""C", a.cool);
      if (a.has_oil) snprintf(c2, sizeof(c2), "%.1f bar", a.oil);
      snprintf(b, sizeof(b), "%s   %s", c1, c2);
      set_label(tw_vals[i], b);
      UiLevel l = a.cool_lvl > a.oil_lvl ? a.cool_lvl : a.oil_lvl;
      value_color(tw_vals[i], NULL, l);
    }
  }

  lv_obj_set_style_bg_color(c_eng, lv_color_hex(any_fault ? 0x5A1414 : COL_CARD), 0);
  card_level(c_eng, any_fault ? UI_OK : eng_lvl);   // a fault already paints the whole card

  /* ---- depth (with the sounder's offset, as on the Nav tab) */
  double off;
  if (n2kGet(Q_DEPTH, 0, 0, v)) {
    bool has_off = n2kGet(Q_DEPTH_OFFSET, 0, 0, off, 30000);
    double d = has_off ? v + off : v;
    snprintf(b, sizeof(b), d < 10 ? "%.1f" : "%.0f", d);
    set_label(v_depth, b);
    set_label(t_depth_ref, !has_off ? "below transducer" : off >= 0 ? "below surface" : "below keel");
    UiLevel l = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_DEPTH, 0, 0), d));
    value_color(v_depth, u_depth, l);
    card_level(c_depth, l);
    alert(l, "DEPTH %.1f m", d);
  } else {
    set_label(v_depth, "--");
    set_label(t_depth_ref, "");
    value_color(v_depth, u_depth, UI_STALE);
    card_level(c_depth, UI_OK);
  }

  /* ---- speed: through water if there is a log, otherwise over ground */
  double stw, sog, cog;
  bool has_stw = n2kGet(Q_STW, 0, 0, stw), has_sog = n2kGet(Q_SOG, 0, 0, sog);
  bool has_cog = n2kGet(Q_COG, 0, 0, cog);
  if (has_stw || has_sog) {
    snprintf(b, sizeof(b), "%.1f", has_stw ? stw : sog);
    set_label(v_speed, b);
    set_label(t_speed_ref, has_stw ? "log" : "SOG");
    value_color(v_speed, u_speed, UI_OK);
  } else {
    set_label(v_speed, "--");
    set_label(t_speed_ref, "");
    value_color(v_speed, u_speed, UI_STALE);
  }
  b[0] = 0;
  if (has_stw && has_sog) snprintf(b, sizeof(b), "SOG %.1f kn", sog);
  if (has_cog) snprintf(b + strlen(b), sizeof(b) - strlen(b), "%sCOG %03.0f\xC2\xB0", b[0] ? "   " : "", cog);
  set_label(l_speed_sub, b);

  /* ---- battery: Victron battery monitor for SOC, volts from it or from NMEA 2000 */
  float soc = NAN, bv = NAN, bi = NAN;
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  for (int i = 0; i < MAX_VIC; i++) {
    const VicData &d = vic_data[i];
    if (vic_cfg[i].used && vic_cfg[i].type == VIC_BATTMON && d.key_ok && vic_fresh(d, now)) {
      soc = d.soc; bv = d.batt_v; bi = d.batt_i;
      break;
    }
  }
  xSemaphoreGive(vic_mtx);
  uint8_t binst = 0;
  if (isnan(bv)) {
    double nv;
    if (NAV_BATT_INSTANCE == 255 ? n2kGetAny(Q_BATT_V, nv, N2K_STALE_MS, &binst)
                                 : n2kGet(Q_BATT_V, binst = NAV_BATT_INSTANCE, 0, nv)) bv = nv;
  }

  if (!isnan(soc)) {
    UiLevel l = soc < START_SOC_ALARM ? UI_ALARM : soc < START_SOC_WARN ? UI_WARN : UI_OK;
    snprintf(b, sizeof(b), "%.0f%%", soc);
    set_label(v_soc, b);
    value_color(v_soc, NULL, l);
    lv_bar_set_value(bar_soc, (int)(soc + 0.5f), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_soc, l == UI_OK ? lv_color_hex(0x38A9F5) : uiLevelColor(l), LV_PART_INDICATOR);
    card_level(c_batt, l);
    alert(l, "BATTERY %.0f%%", soc);
  } else {
    set_label(v_soc, "--");
    value_color(v_soc, NULL, UI_STALE);
    lv_bar_set_value(bar_soc, 0, LV_ANIM_OFF);
    card_level(c_batt, UI_OK);
  }
  set_label(l_charge, (!isnan(bi) && bi > 0.05f) ? LV_SYMBOL_CHARGE : "");

  if (!isnan(bv)) {
    UiLevel l = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_BATT_V, binst, 0), bv));
    snprintf(b, sizeof(b), "%.2f", bv);
    set_label(v_volt, b);
    value_color(v_volt, NULL, l);
    card_level(c_volt, l);
    alert(l, "BATTERY %.2f V", bv);
  } else {
    set_label(v_volt, "--");
    value_color(v_volt, NULL, UI_STALE);
    card_level(c_volt, UI_OK);
  }
  if (!isnan(bi)) {
    snprintf(b, sizeof(b), "%+.1f A", bi);
    set_label(l_amp, b);
    lv_obj_set_style_text_color(l_amp, bi > 0.05f ? lv_palette_main(LV_PALETTE_GREEN) : lv_color_white(), 0);
    snprintf(b, sizeof(b), "%+.0f W", bv * bi);
    set_label(l_watt, isnan(bv) ? "" : b);
  } else {
    set_label(l_amp, "");
    set_label(l_watt, "");
  }

  /* ---- tanks: colour from their limits; only alarms go to the banner */
  if (tk_count < 0 || tk_ver != n2kTankListVersion()) build_tanks();
  UiLevel tanks_lvl = UI_OK;
  for (int i = 0; i < tk_count; i++) {
    N2kTank *tk = n2kTankAt(i);
    double pct;
    if (tk && n2kGet(Q_TANK_LEVEL, tk->instance, tk->fluidType, pct)) {
      UiLevel l = uiLevelFrom(n2kLimitsEval(n2kLimitsGet(Q_TANK_LEVEL, tk->instance, tk->fluidType), pct));
      lv_bar_set_value(tk_bar[i], (int)(pct + 0.5), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(tk_bar[i], l == UI_OK ? lv_color_hex(tk->fluidType == 5 ? 0x8A6A4C : 0x3A7EC6)
                                                      : uiLevelColor(l), LV_PART_INDICATOR);
      snprintf(b, sizeof(b), "%.0f%%", pct);
      set_label(tk_pct[i], b);
      value_color(tk_pct[i], NULL, l);
      if (l == UI_ALARM) {
        char nm[24];
        strlcpy(nm, tk->name, sizeof(nm));
        for (char *p = nm; *p; p++) *p = toupper(*p);
        alert(l, "%s %.0f%%", nm, pct);
      }
      if (l > tanks_lvl) tanks_lvl = l;
    } else {
      lv_bar_set_value(tk_bar[i], 0, LV_ANIM_OFF);
      set_label(tk_pct[i], "--");
      value_color(tk_pct[i], NULL, UI_STALE);
    }
  }
  card_level(c_tanks, tanks_lvl);

  /* ---- banner: most important message, alarms before warnings */
  int top = -1;
  for (int i = 0; i < n_alerts && top < 0; i++) if (alerts[i].lvl == UI_ALARM) top = i;
  for (int i = 0; i < n_alerts && top < 0; i++) if (alerts[i].lvl == UI_WARN) top = i;
  if (top < 0) {
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_HIDDEN);
    ack_text[0] = 0;
    last_alarm[0] = 0;
    return;
  }

  const StartAlert &a = alerts[top];
  snprintf(b, sizeof(b), LV_SYMBOL_WARNING "  %s", a.text);
  set_label(lbl_banner, b);
  if (n_alerts > 1) snprintf(b, sizeof(b), "+%d more", n_alerts - 1);
  else b[0] = 0;
  bool acked = strcmp(ack_text, lv_label_get_text(lbl_banner)) == 0;
  if (a.lvl == UI_ALARM && !acked) strlcat(b, n_alerts > 1 ? "   tap = ack" : "tap = ack", sizeof(b));
  set_label(lbl_banner_more, b);
  lv_obj_add_flag(hdr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);

  uint32_t col = a.lvl == UI_WARN ? COL_BANNER_WARN
               : (!acked && blink_on) ? COL_BANNER_BLINK : COL_BANNER_ALARM;
  lv_obj_set_style_bg_color(banner, lv_color_hex(col), 0);

  /* a new, unacknowledged alarm: wake the display and show this page */
  if (a.lvl == UI_ALARM && !acked) {
    lv_disp_trig_activity(NULL);                     // keeps the screen saver away
    if (strcmp(last_alarm, a.text) != 0) {
      strlcpy(last_alarm, a.text, sizeof(last_alarm));
      logf("ALARM: %s", a.text);
      saver_wake();
      if (ui_active_tab() != 2) ui_show_tab(0);      // leave the Engine tab alone: it shows it too
    }
  }
}
