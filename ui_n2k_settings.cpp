// ui_n2k_settings.cpp
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "ui_n2k_settings.h"
#include "ui_n2k_common.h"
#include "n2k_config.h"
#include "n2k_data.h"
#include "n2k_limits.h"

static const uint32_t COL_DEV  = 0x1E3A5F;
static const uint32_t COL_ROW  = 0x16263A;
static const uint32_t COL_DIM  = 0x8FA3B8;

struct Row { lv_obj_t *val, *mark; uint8_t q, inst, sub; };

static lv_obj_t *s_tab, *s_list;
static Row s_rows[N2K_MAX_CHANNELS];
static int s_rowN = 0;
static int s_builtSeen = -1, s_builtCh = -1;
static uint32_t s_builtLimVer = 0;

// editor
static lv_obj_t *s_ov = nullptr, *s_kb, *s_err, *s_ta[6];
static uint8_t s_eq, s_ei, s_es;
// field order in the grid: left column = low side, right column = high side
static const char *FIELD_NAME[6] = { "Gauge min", "Gauge max", "Warn low", "Warn high", "Alarm low", "Alarm high" };

// ------------------------------------------------------------------ helpers
static lv_obj_t *box(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t bg) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, bg ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  return b;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t col) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

static void chanName(uint8_t q, uint8_t inst, uint8_t sub, char *b, size_t n) {
  if (q == Q_TANK_LEVEL || q == Q_TANK_CAP) {
    const char *tn = n2kFluidName(sub);
    for (int i = 0; i < n2kTankCount(); i++) {
      N2kTank *t = n2kTankAt(i);
      if (t->fluidType == sub && t->instance == inst) { tn = t->name; break; }
    }
    snprintf(b, n, "%s - %s", tn, q == Q_TANK_LEVEL ? "level" : "capacity");
  } else if (q >= Q_ENG_RPM && q <= Q_ENG_STATUS2) {
    snprintf(b, n, "%s  (engine #%u)", n2kQtyLabel(q), inst);
  } else if (q == Q_TEMP) {
    snprintf(b, n, "Temperature src %u #%u", sub, inst);
  } else if (q == Q_BATT_V || q == Q_BATT_A || q == Q_SEA_TEMP) {
    snprintf(b, n, "%s #%u", n2kQtyLabel(q), inst);
  } else {
    snprintf(b, n, "%s", n2kQtyLabel(q));
  }
}

static void fmtValue(uint8_t q, double v, char *b, size_t n) {
  if (q == Q_LAT || q == Q_LON)                 snprintf(b, n, "%.5f", v);
  else if (q == Q_ENG_STATUS1 || q == Q_ENG_STATUS2) snprintf(b, n, "0x%04X", (unsigned)v);
  else if (fabs(v) >= 100)                      snprintf(b, n, "%.0f %s", v, n2kQtyUnit(q));
  else                                          snprintf(b, n, "%.2f %s", v, n2kQtyUnit(q));
}

static void fmtLimit(float v, char *b, size_t n) {
  if (isnan(v)) b[0] = 0;
  else snprintf(b, n, "%g", v);
}

// ------------------------------------------------------------------ editor
static void closeEditor() {
  if (s_ov) { lv_obj_del(s_ov); s_ov = nullptr; }
}

static float readField(int i) {
  const char *t = lv_textarea_get_text(s_ta[i]);
  while (*t == ' ') t++;
  if (!*t) return NAN;
  return strtof(t, nullptr);
}

static void onSave(lv_event_t *) {
  // grid order -> struct fields
  N2kLimits l;
  l.gMin = readField(0); l.gMax = readField(1);
  l.warnLo = readField(2); l.warnHi = readField(3);
  l.alarmLo = readField(4); l.alarmHi = readField(5);
  const char *err;
  if (!n2kLimitsValid(l, &err)) { lv_label_set_text(s_err, err); return; }
  if (!n2kLimitsSet(s_eq, s_ei, s_es, l)) { lv_label_set_text(s_err, "Too many custom limits - reset some first"); return; }
  closeEditor();
}

static void onDefaults(lv_event_t *) { n2kLimitsReset(s_eq, s_ei, s_es); closeEditor(); }
static void onCancel(lv_event_t *)   { closeEditor(); }

static void onTaFocus(lv_event_t *e) { lv_keyboard_set_textarea(s_kb, lv_event_get_target(e)); }

static void onTaReady(lv_event_t *e) {           // keyboard OK -> next field
  lv_obj_t *ta = lv_event_get_target(e);
  for (int i = 0; i < 5; i++)
    if (s_ta[i] == ta) { lv_obj_add_state(s_ta[i + 1], LV_STATE_FOCUSED); lv_obj_clear_state(ta, LV_STATE_FOCUSED);
                         lv_keyboard_set_textarea(s_kb, s_ta[i + 1]); return; }
}

static lv_obj_t *button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, uint32_t col) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, 130, 38);
  lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

static void openEditor(uint8_t q, uint8_t inst, uint8_t sub) {
  closeEditor();
  s_eq = q; s_ei = inst; s_es = sub;
  N2kLimits cur = n2kLimitsGet(q, inst, sub);
  float vals[6] = { cur.gMin, cur.gMax, cur.warnLo, cur.warnHi, cur.alarmLo, cur.alarmHi };

  s_ov = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_clear_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_radius(s_ov, 0, 0);
  lv_obj_set_style_pad_all(s_ov, 0, 0);

  lv_obj_t *p = box(s_ov, lv_pct(98), 298, COL_ROW);
  lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(p, 4, 0);
  lv_obj_set_style_pad_column(p, 8, 0);

  char name[48], title[80];
  chanName(q, inst, sub, name, sizeof(name));
  snprintf(title, sizeof(title), "%s  [%s]%s", name, n2kQtyUnit(q),
           n2kLimitsIsCustom(q, inst, sub) ? "  - custom" : "  - default");
  lv_obj_t *t = label(p, title, UI_FONT_M, 0xFFFFFF);
  lv_obj_set_width(t, lv_pct(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);

  for (int i = 0; i < 6; i++) {
    lv_obj_t *f = box(p, lv_pct(48), 60, 0);
    lv_obj_set_style_pad_all(f, 0, 0);
    label(f, FIELD_NAME[i], UI_FONT_S, COL_DIM);
    s_ta[i] = lv_textarea_create(f);
    lv_textarea_set_one_line(s_ta[i], true);
    lv_textarea_set_accepted_chars(s_ta[i], "0123456789.-");
    lv_textarea_set_max_length(s_ta[i], 9);
    lv_textarea_set_placeholder_text(s_ta[i], i < 2 ? "required" : "off");
    lv_obj_set_size(s_ta[i], lv_pct(100), 40);
    lv_obj_align(s_ta[i], LV_ALIGN_BOTTOM_MID, 0, 0);
    char b[16]; fmtLimit(vals[i], b, sizeof(b));
    lv_textarea_set_text(s_ta[i], b);
    lv_obj_add_event_cb(s_ta[i], onTaFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(s_ta[i], onTaFocus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ta[i], onTaReady, LV_EVENT_READY, nullptr);
  }

  s_err = label(p, "Empty warn/alarm field = off", UI_FONT_S, 0xE0A020);
  lv_obj_set_width(s_err, lv_pct(100));

  lv_obj_t *btns = box(p, lv_pct(100), 42, 0);
  lv_obj_set_style_pad_all(btns, 0, 0);
  lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  button(btns, LV_SYMBOL_SAVE " Save", onSave, 0x2E7D32);
  button(btns, LV_SYMBOL_REFRESH " Defaults", onDefaults, 0x455A64);
  button(btns, LV_SYMBOL_CLOSE " Cancel", onCancel, 0x6D4C41);

  s_kb = lv_keyboard_create(s_ov);
  lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(s_kb, lv_pct(100), 176);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_state(s_ta[0], LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_kb, s_ta[0]);
}

static void onRowClick(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= 0 && i < s_rowN) openEditor(s_rows[i].q, s_rows[i].inst, s_rows[i].sub);
}

// ------------------------------------------------------------------ list
static int cmpU32(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}

static void build() {
  lv_coord_t scrollY = lv_obj_get_scroll_y(s_tab);
  lv_obj_clean(s_list);
  s_rowN = 0;
  s_builtSeen = n2kSeenCount();
  s_builtCh = n2kChannelCount();
  s_builtLimVer = n2kLimitsVersion();

  // snapshot of the sniffer
  static N2kSeenPgn seen[N2K_MAX_SEEN_PGNS];
  int ns = 0;
  for (int i = 0; i < n2kSeenCount() && ns < N2K_MAX_SEEN_PGNS; i++)
    if (n2kSeenCopy(i, seen[ns])) ns++;

  if (ns == 0) {
    label(s_list, "No NMEA 2000 traffic received yet", UI_FONT_M, COL_DIM);
    return;
  }

  // unique source addresses, sorted
  uint32_t srcs[N2K_MAX_SEEN_PGNS]; int nsrc = 0;
  for (int i = 0; i < ns; i++) {
    bool have = false;
    for (int k = 0; k < nsrc; k++) if (srcs[k] == seen[i].src) have = true;
    if (!have) srcs[nsrc++] = seen[i].src;
  }
  qsort(srcs, nsrc, sizeof(uint32_t), cmpU32);

  char b1[64], b2[48];
  for (int si = 0; si < nsrc; si++) {
    uint8_t src = (uint8_t)srcs[si];

    // ---- device header
    lv_obj_t *dev = box(s_list, lv_pct(100), 40, COL_DEV);
    n2kDeviceLabel(src, b1, sizeof(b1));
    lv_obj_t *dl = label(dev, b1, UI_FONT_M, 0xFFFFFF);
    lv_obj_align(dl, LV_ALIGN_LEFT_MID, 2, 0);
    snprintf(b2, sizeof(b2), "src %u", src);
    lv_obj_align(label(dev, b2, UI_FONT_S, COL_DIM), LV_ALIGN_RIGHT_MID, -2, 0);

    // ---- PGNs of this device, sorted
    uint32_t pgns[N2K_MAX_SEEN_PGNS]; uint32_t counts[N2K_MAX_SEEN_PGNS]; int np = 0;
    for (int i = 0; i < ns; i++) if (seen[i].src == src) { pgns[np] = seen[i].pgn; np++; }
    qsort(pgns, np, sizeof(uint32_t), cmpU32);
    for (int k = 0; k < np; k++) {
      counts[k] = 0;
      for (int i = 0; i < ns; i++) if (seen[i].src == src && seen[i].pgn == pgns[k]) counts[k] = seen[i].count;
    }

    for (int pi = 0; pi < np; pi++) {
      uint32_t pgn = pgns[pi];
      lv_obj_t *ph = box(s_list, lv_pct(100), 26, 0);
      lv_obj_set_style_pad_left(ph, 12, 0);
      const char *pn = n2kPgnName(pgn);
      snprintf(b1, sizeof(b1), "PGN %lu  %s", (unsigned long)pgn, pn);
      lv_obj_align(label(ph, b1, UI_FONT_S, 0x9CC8F0), LV_ALIGN_LEFT_MID, 0, 0);
      snprintf(b2, sizeof(b2), "%lu msg", (unsigned long)counts[pi]);
      lv_obj_align(label(ph, b2, UI_FONT_S, COL_DIM), LV_ALIGN_RIGHT_MID, 0, 0);

      // values decoded from this PGN by this device
      int shown = 0;
      N2kChannel c;
      for (int ci = 0; ci < n2kChannelCount(); ci++) {
        if (!n2kChannelCopy(ci, c) || c.src != src || c.pgn != pgn) continue;
        if (s_rowN >= N2K_MAX_CHANNELS) break;
        shown++;
        Row &r = s_rows[s_rowN];
        r.q = c.qty; r.inst = c.instance; r.sub = c.sub;
        bool editable = n2kQtyHasLimits(c.qty);

        lv_obj_t *row = box(s_list, lv_pct(100), 40, COL_ROW);
        lv_obj_set_style_pad_left(row, 20, 0);
        chanName(c.qty, c.instance, c.sub, b1, sizeof(b1));
        lv_obj_t *nl = label(row, b1, UI_FONT_S, 0xFFFFFF);
        lv_obj_set_width(nl, lv_pct(58));
        lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
        lv_obj_align(nl, LV_ALIGN_LEFT_MID, 0, 0);

        r.val = label(row, "--", UI_FONT_M, 0xFFFFFF);
        lv_obj_align(r.val, LV_ALIGN_RIGHT_MID, editable ? -34 : 0, 0);

        r.mark = nullptr;
        if (editable) {
          r.mark = label(row, LV_SYMBOL_SETTINGS, UI_FONT_M, COL_DIM);
          lv_obj_align(r.mark, LV_ALIGN_RIGHT_MID, 0, 0);
          if (n2kLimitsIsCustom(c.qty, c.instance, c.sub))
            lv_obj_set_style_text_color(r.mark, lv_palette_main(LV_PALETTE_ORANGE), 0);
          lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_add_event_cb(row, onRowClick, LV_EVENT_CLICKED, (void *)(intptr_t)s_rowN);
          lv_obj_set_style_bg_color(row, lv_color_hex(0x223449), LV_STATE_PRESSED);
        }
        s_rowN++;
      }
      if (!shown) {
        lv_obj_t *nd = label(s_list, "   not decoded", UI_FONT_S, 0x5A6B7D);
        lv_obj_set_style_pad_left(nd, 20, 0);
      }
    }
  }
  lv_obj_update_layout(s_tab);
  lv_obj_scroll_to_y(s_tab, scrollY, LV_ANIM_OFF);
}

static void tick(lv_timer_t *) {
  if (s_ov) return;                                  // don't rebuild under the editor
  if (!lv_obj_is_visible(s_tab)) return;             // only work while the tab is shown
  if (s_builtSeen != n2kSeenCount() || s_builtCh != n2kChannelCount() ||
      s_builtLimVer != n2kLimitsVersion()) build();

  char b[32];
  for (int i = 0; i < s_rowN; i++) {
    Row &r = s_rows[i];
    double v;
    if (n2kGet((N2kQty)r.q, r.inst, r.sub, v, 60000)) fmtValue(r.q, v, b, sizeof(b));
    else strcpy(b, "--");
    uiLabelSetIfChanged(r.val, b);
  }
}

static void onRefresh(lv_event_t *) { build(); }

void ui_n2k_settings_create(lv_obj_t *tab) {
  s_tab = tab;
  uiPrepTab(tab);
  lv_obj_set_style_bg_color(tab, lv_color_black(), 0);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 4, 0);

  lv_obj_t *hdr = box(tab, lv_pct(100), 44, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_align(label(hdr, "NMEA 2000 devices", UI_FONT_L, 0xFFFFFF), LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_t *rb = lv_btn_create(hdr);
  lv_obj_set_size(rb, 44, 38);
  lv_obj_align(rb, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(rb, onRefresh, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(rb, LV_SYMBOL_REFRESH, UI_FONT_M, 0xFFFFFF));

  label(tab, LV_SYMBOL_SETTINGS " = tap to set gauge range and limits (orange = custom)", UI_FONT_S, COL_DIM);

  s_list = box(tab, lv_pct(100), LV_SIZE_CONTENT, 0);
  lv_obj_set_style_pad_all(s_list, 0, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list, 3, 0);

  build();
  lv_timer_create(tick, 1000, nullptr);
}
