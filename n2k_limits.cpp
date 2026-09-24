// n2k_limits.cpp
#include <Preferences.h>
#include "n2k_limits.h"
#include "n2k_config.h"

struct LimRec { uint8_t q, inst, sub, used; N2kLimits l; };

static LimRec s_rec[N2K_MAX_LIMIT_OVERRIDES];
static volatile uint32_t s_ver = 1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t REC_FORMAT = 1;

static const N2kLimits NONE = { NAN, NAN, NAN, NAN, NAN, NAN };

N2kLimits n2kLimitsDefault(uint8_t q, uint8_t sub) {
  switch (q) {
    case Q_DEPTH:         return { 0, 50, NAV_DEPTH_ALARM_M, NAV_DEPTH_WARN_M, NAN, NAN };
    case Q_STW:
    case Q_SOG:           return { 0, 15, NAN, NAN, NAN, NAN };
    case Q_SEA_TEMP:      return { -2, 30, NAN, NAN, NAN, NAN };
    case Q_TEMP:          return { -20, 100, NAN, NAN, NAN, NAN };
    case Q_BATT_V:        return { 10, 16, 11.5f, NAV_BATT_LOW_V, 14.8f, 15.2f };
    case Q_BATT_A:        return { -100, 100, NAN, NAN, NAN, NAN };
    case Q_ENG_RPM:       return { 0, ENG_RPM_MAX_SCALE, NAN, NAN, NAN, ENG_RPM_RED_FROM };
    case Q_ENG_OIL_P:     return { 0, ENG_OIL_P_SCALE_MAX, ENG_OIL_P_MIN_BAR, NAN, NAN, NAN };
    case Q_ENG_OIL_T:     return { 40, 140, NAN, NAN, ENG_OIL_TEMP_WARN_C, 125 };
    case Q_ENG_COOL_T:    return { ENG_COOLANT_SCALE_MIN, ENG_COOLANT_SCALE_MAX, NAN, NAN,
                                   ENG_COOLANT_WARN_C, ENG_COOLANT_ALARM_C };
    case Q_ENG_ALT_V:     return { ENG_VOLT_SCALE_MIN, ENG_VOLT_SCALE_MAX, 12.0f, ENG_ALT_V_LOW,
                                   ENG_ALT_V_HIGH, 15.5f };
    case Q_ENG_FUEL_RATE: return { 0, 30, NAN, NAN, NAN, NAN };
    case Q_TANK_LEVEL:
      if (n2kFluidIsWaste(sub))
        return { 0, 100, NAN, NAN, TANK_WASTE_WARN_PCT, TANK_WASTE_ALARM_PCT };
      return { 0, 100, TANK_SUPPLY_ALARM_PCT, TANK_SUPPLY_WARN_PCT, NAN, NAN };
    default:              return NONE;   // position, COG, hours, status bits, capacity ...
  }
}

bool n2kQtyHasLimits(uint8_t q) { return !isnan(n2kLimitsDefault(q, 0).gMin); }

static int findRec(uint8_t q, uint8_t inst, uint8_t sub) {
  for (int i = 0; i < N2K_MAX_LIMIT_OVERRIDES; i++)
    if (s_rec[i].used && s_rec[i].q == q && s_rec[i].inst == inst && s_rec[i].sub == sub) return i;
  return -1;
}

static void save() {
  Preferences p;
  p.begin("n2klim", false);
  p.putUChar("fmt", REC_FORMAT);
  p.putBytes("recs", s_rec, sizeof(s_rec));
  p.end();
}

void n2kLimitsInit() {
  memset(s_rec, 0, sizeof(s_rec));
  Preferences p;
  p.begin("n2klim", false);
  if (p.getUChar("fmt", 0) == REC_FORMAT && p.getBytesLength("recs") == sizeof(s_rec))
    p.getBytes("recs", s_rec, sizeof(s_rec));
  p.end();
  s_ver++;
}

N2kLimits n2kLimitsGet(uint8_t q, uint8_t inst, uint8_t sub) {
  N2kLimits l;
  portENTER_CRITICAL(&s_mux);
  int i = findRec(q, inst, sub);
  bool found = i >= 0;
  if (found) l = s_rec[i].l;
  portEXIT_CRITICAL(&s_mux);
  return found ? l : n2kLimitsDefault(q, sub);
}

bool n2kLimitsIsCustom(uint8_t q, uint8_t inst, uint8_t sub) {
  portENTER_CRITICAL(&s_mux);
  bool f = findRec(q, inst, sub) >= 0;
  portEXIT_CRITICAL(&s_mux);
  return f;
}

bool n2kLimitsSet(uint8_t q, uint8_t inst, uint8_t sub, const N2kLimits &l) {
  portENTER_CRITICAL(&s_mux);
  int i = findRec(q, inst, sub);
  if (i < 0) for (int k = 0; k < N2K_MAX_LIMIT_OVERRIDES; k++) if (!s_rec[k].used) { i = k; break; }
  if (i >= 0) {
    s_rec[i].q = q; s_rec[i].inst = inst; s_rec[i].sub = sub; s_rec[i].used = 1;
    s_rec[i].l = l;
  }
  portEXIT_CRITICAL(&s_mux);
  if (i < 0) return false;
  save();
  s_ver++;
  return true;
}

void n2kLimitsReset(uint8_t q, uint8_t inst, uint8_t sub) {
  portENTER_CRITICAL(&s_mux);
  int i = findRec(q, inst, sub);
  if (i >= 0) s_rec[i].used = 0;
  portEXIT_CRITICAL(&s_mux);
  if (i >= 0) { save(); s_ver++; }
}

uint32_t n2kLimitsVersion() { return s_ver; }

N2kLevel n2kLimitsEval(const N2kLimits &l, double v, bool checkLow) {
  if (!isnan(l.alarmHi) && v >= l.alarmHi) return N2K_LVL_ALARM;
  if (checkLow && !isnan(l.alarmLo) && v <= l.alarmLo) return N2K_LVL_ALARM;
  if (!isnan(l.warnHi) && v >= l.warnHi) return N2K_LVL_WARN;
  if (checkLow && !isnan(l.warnLo) && v <= l.warnLo) return N2K_LVL_WARN;
  return N2K_LVL_OK;
}

bool n2kLimitsValid(const N2kLimits &l, const char **err) {
  const char *e = nullptr;
  float seq[4] = { l.alarmLo, l.warnLo, l.warnHi, l.alarmHi };
  if (isnan(l.gMin) || isnan(l.gMax))      e = "Gauge min and max are required";
  else if (l.gMin >= l.gMax)               e = "Gauge min must be below max";
  else {
    float prev = NAN;
    for (int i = 0; i < 4 && !e; i++) {
      if (isnan(seq[i])) continue;
      if (!isnan(prev) && seq[i] < prev)   e = "Order: alarm low <= warn low < warn high <= alarm high";
      prev = seq[i];
    }
    if (!e && !isnan(l.warnLo) && !isnan(l.warnHi) && l.warnLo >= l.warnHi)
      e = "Warn low must be below warn high";
  }
  if (err) *err = e;
  return e == nullptr;
}
