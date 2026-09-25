// n2k_web.cpp - JSON status, settings API and CSV logging helpers.
// Server-agnostic: works with WebServer, AsyncWebServer or anything that can send a String.
#include "n2k_bus.h"
#include "n2k_data.h"
#include "n2k_settings.h"
#include "n2k_limits.h"

static void jstr(String &o, const char *s) {
  o += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  o += '"';
}

static void jnum(String &o, float v, int dec) {
  if (isnan(v)) o += "null"; else o += String(v, dec);
}

static void jlimits(String &o, uint8_t q, uint8_t inst, uint8_t sub) {
  N2kLimits l = n2kLimitsGet(q, inst, sub);
  o += "{\"gauge_min\":"; jnum(o, l.gMin, 2);
  o += ",\"gauge_max\":";  jnum(o, l.gMax, 2);
  o += ",\"alarm_low\":";  jnum(o, l.alarmLo, 2);
  o += ",\"warn_low\":";   jnum(o, l.warnLo, 2);
  o += ",\"warn_high\":";  jnum(o, l.warnHi, 2);
  o += ",\"alarm_high\":"; jnum(o, l.alarmHi, 2);
  o += ",\"custom\":";     o += n2kLimitsIsCustom(q, inst, sub) ? "true" : "false";
  o += '}';
}

// Devices -> PGNs -> decoded values (with limits where applicable)
String n2kJson() {
  String o;
  o.reserve(12000);
  uint32_t now = millis();

  o += "{\"bus\":{\"ok\":";  o += n2kBusOk() ? "true" : "false";
  o += ",\"msgs\":";        o += n2kMsgCount();
  o += "},\"engine\":{\"instance\":"; o += n2kSettings.engInstance;
  o += ",\"source\":";      o += n2kSettings.engSource;
  o += ",\"name\":";        jstr(o, n2kSettings.engName);
  o += ",\"twin\":";        o += n2kSettings.twin ? "true" : "false";
  o += ",\"port_name\":";   jstr(o, n2kSettings.portName);
  o += ",\"stbd_name\":";   jstr(o, n2kSettings.stbdName);
  o += "},\"devices\":[";

  static N2kSeenPgn seen[N2K_MAX_SEEN_PGNS];
  int ns = 0;
  for (int i = 0; i < n2kSeenCount() && ns < N2K_MAX_SEEN_PGNS; i++) if (n2kSeenCopy(i, seen[ns])) ns++;

  bool firstDev = true;
  for (int src = 0; src < 254; src++) {
    bool has = false;
    for (int i = 0; i < ns; i++) if (seen[i].src == src) { has = true; break; }
    if (!has) continue;
    if (!firstDev) o += ',';
    firstDev = false;

    char name[64];
    n2kDeviceLabel(src, name, sizeof(name));
    N2kDevice d;
    bool known = n2kDeviceCopy(src, d);
    o += "{\"src\":"; o += src;
    o += ",\"name\":"; jstr(o, name);
    if (known) { o += ",\"mfr\":"; o += d.mfr; o += ",\"model\":"; jstr(o, d.model); }
    o += ",\"pgns\":[";

    bool firstPgn = true;
    for (int i = 0; i < ns; i++) {
      if (seen[i].src != src) continue;
      if (!firstPgn) o += ',';
      firstPgn = false;
      o += "{\"pgn\":"; o += seen[i].pgn;
      o += ",\"name\":"; jstr(o, n2kPgnName(seen[i].pgn));
      o += ",\"n\":";   o += seen[i].count;
      o += ",\"age\":"; o += (now - seen[i].lastMs);
      o += ",\"values\":[";
      bool firstCh = true;
      N2kChannel c;
      for (int ci = 0; ci < n2kChannelCount(); ci++) {
        if (!n2kChannelCopy(ci, c) || c.src != src || c.pgn != seen[i].pgn) continue;
        if (!firstCh) o += ',';
        firstCh = false;
        o += "{\"q\":\"";  o += n2kQtyName(c.qty);
        o += "\",\"label\":"; jstr(o, n2kQtyLabel(c.qty));
        o += ",\"unit\":\""; o += n2kQtyUnit(c.qty);
        o += "\",\"inst\":"; o += c.instance;
        o += ",\"sub\":";    o += c.sub;
        o += ",\"v\":";      o += String(c.value, (c.qty == Q_LAT || c.qty == Q_LON) ? 6 : 2);
        o += ",\"age\":";    o += (now - c.lastMs);
        if (n2kQtyHasLimits(c.qty)) { o += ",\"limits\":"; jlimits(o, c.qty, c.instance, c.sub); }
        o += '}';
      }
      o += "]}";
    }
    o += "]}";
  }

  o += "],\"tanks\":[";
  for (int i = 0; i < n2kTankCount(); i++) {
    N2kTank *t = n2kTankAt(i);
    double lv, cap;
    if (i) o += ',';
    o += "{\"type\":"; o += t->fluidType;
    o += ",\"typeName\":"; jstr(o, n2kFluidName(t->fluidType));
    o += ",\"inst\":"; o += t->instance;
    o += ",\"name\":"; jstr(o, t->name);
    o += ",\"level\":"; o += n2kGet(Q_TANK_LEVEL, t->instance, t->fluidType, lv) ? String(lv, 1) : "null";
    o += ",\"capacity\":"; o += n2kGet(Q_TANK_CAP, t->instance, t->fluidType, cap) ? String(cap, 0) : "null";
    o += '}';
  }
  o += "]}";
  return o;
}

// Split "a,b,c,..." into up to max fields (empty fields kept)
static int splitCsv(const String &v, String *out, int max) {
  int n = 0, start = 0;
  while (n < max) {
    int c = v.indexOf(',', start);
    out[n++] = c < 0 ? v.substring(start) : v.substring(start, c);
    if (c < 0) break;
    start = c + 1;
  }
  return n;
}

static bool parseKey(const String *f, uint8_t &q, uint8_t &inst, uint8_t &sub) {
  int qi = n2kQtyFromName(f[0].c_str());
  if (qi < 0 || !n2kQtyHasLimits(qi)) return false;
  q = qi; inst = f[1].toInt(); sub = f[2].toInt();
  return true;
}

static float parseLimit(const String &s) {
  String t = s; t.trim();
  return t.length() ? t.toFloat() : NAN;
}

// Keys:
//   engine_instance = 0..252
//   engine_name     = text (ASCII)
//   engine_source   = 0..251, or 255 for any source
//   twin_engines    = 0 / 1   start page shows port and starboard RPM
//   port_name, stbd_name = text on the two dials
//   tank_name       = "<fluidType>,<instance>,<name>"   e.g. "1,0,Fresh water"
//   limits          = "<q>,<inst>,<sub>,<gauge_min>,<gauge_max>,<alarm_low>,<warn_low>,<warn_high>,<alarm_high>"
//                     empty field = off, e.g. "eng_coolant_t,0,0,40,120,,,90,98"
//   limits_reset    = "<q>,<inst>,<sub>"   back to defaults
bool n2kApplySetting(const String &key, const String &value) {
  if (key == "engine_instance") {
    int v = value.toInt();
    if (v < 0 || v > 252) return false;
    n2kSetEngine((uint8_t)v, n2kSettings.engName, n2kSettings.engSource);
    return true;
  }
  if (key == "engine_name") {
    if (!value.length()) return false;
    n2kSetEngine(n2kSettings.engInstance, value.c_str(), n2kSettings.engSource);
    return true;
  }
  if (key == "engine_source") {
    int v = value.toInt();
    if (v < 0 || (v > 251 && v != 255)) return false;
    n2kSetEngine(n2kSettings.engInstance, n2kSettings.engName, (uint8_t)v);
    return true;
  }
  if (key == "limits") {
    String f[9];
    if (splitCsv(value, f, 9) != 9) return false;
    uint8_t q, inst, sub;
    if (!parseKey(f, q, inst, sub)) return false;
    N2kLimits l = { parseLimit(f[3]), parseLimit(f[4]), parseLimit(f[5]),
                    parseLimit(f[6]), parseLimit(f[7]), parseLimit(f[8]) };
    if (!n2kLimitsValid(l, nullptr)) return false;
    return n2kLimitsSet(q, inst, sub, l);
  }
  if (key == "limits_reset") {
    String f[3];
    if (splitCsv(value, f, 3) != 3) return false;
    uint8_t q, inst, sub;
    if (!parseKey(f, q, inst, sub)) return false;
    n2kLimitsReset(q, inst, sub);
    return true;
  }
  if (key == "twin_engines") {
    n2kSetTwin(value.toInt() != 0, nullptr, nullptr);
    return true;
  }
  if (key == "port_name" || key == "stbd_name") {
    if (!value.length()) return false;
    bool port = key == "port_name";
    n2kSetTwin(n2kSettings.twin, port ? value.c_str() : nullptr, port ? nullptr : value.c_str());
    return true;
  }
  if (key == "tank_name") {
    int c1 = value.indexOf(','), c2 = value.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;
    int ft = value.substring(0, c1).toInt();
    int inst = value.substring(c1 + 1, c2).toInt();
    String name = value.substring(c2 + 1);
    if (ft < 0 || ft > 15 || inst < 0 || inst > 252 || !name.length()) return false;
    n2kSetTankName((uint8_t)ft, (uint8_t)inst, name.c_str());
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- SD logging
static void csvVal(String &o, bool ok, double v, int dec) {
  o += ',';
  if (ok) o += String(v, dec);
}

String n2kCsvHeader() {
  String h = "depth_m,stw_kn,sog_kn,cog_deg,sea_c,batt_v,lat,lon,rpm,coolant_c,oil_bar,oil_c,alt_v,fuel_lh,hours";
  for (int i = 0; i < n2kTankCount(); i++) {
    N2kTank *t = n2kTankAt(i);
    h += ",tank_"; h += t->fluidType; h += '_'; h += t->instance; h += "_pct";
  }
  return h;
}

// Tanks added after the header was written are appended at the end of later lines;
// start a new log file when n2kTankListVersion() changes if you need strict columns.
String n2kCsvLine() {
  String o;
  double v;
  uint8_t e = n2kSettings.engInstance;
  bool ok = n2kGet(Q_DEPTH, 0, 0, v); if (ok) o += String(v, 2);
  ok = n2kGet(Q_STW, 0, 0, v);           csvVal(o, ok, v, 2);
  ok = n2kGet(Q_SOG, 0, 0, v);           csvVal(o, ok, v, 2);
  ok = n2kGet(Q_COG, 0, 0, v);           csvVal(o, ok, v, 0);
  ok = n2kGetAny(Q_SEA_TEMP, v);         csvVal(o, ok, v, 1);
  ok = n2kGetAny(Q_BATT_V, v);           csvVal(o, ok, v, 2);
  ok = n2kGet(Q_LAT, 0, 0, v);           csvVal(o, ok, v, 6);
  ok = n2kGet(Q_LON, 0, 0, v);           csvVal(o, ok, v, 6);
  ok = n2kGet(Q_ENG_RPM, e, 0, v);       csvVal(o, ok, v, 0);
  ok = n2kGet(Q_ENG_COOL_T, e, 0, v);    csvVal(o, ok, v, 1);
  ok = n2kGet(Q_ENG_OIL_P, e, 0, v);     csvVal(o, ok, v, 2);
  ok = n2kGet(Q_ENG_OIL_T, e, 0, v);     csvVal(o, ok, v, 1);
  ok = n2kGet(Q_ENG_ALT_V, e, 0, v);     csvVal(o, ok, v, 2);
  ok = n2kGet(Q_ENG_FUEL_RATE, e, 0, v); csvVal(o, ok, v, 1);
  ok = n2kGet(Q_ENG_HOURS, e, 0, v, 60000); csvVal(o, ok, v, 1);
  for (int i = 0; i < n2kTankCount(); i++) {
    N2kTank *t = n2kTankAt(i);
    ok = n2kGet(Q_TANK_LEVEL, t->instance, t->fluidType, v); csvVal(o, ok, v, 0);
  }
  return o;
}
