// n2k_data.cpp
#include "n2k_data.h"
#include "n2k_settings.h"

static N2kChannel s_ch[N2K_MAX_CHANNELS];
static volatile int s_chCount = 0;

static N2kSeenPgn s_seen[N2K_MAX_SEEN_PGNS];
static volatile int s_seenCount = 0;

static N2kTank s_tanks[N2K_MAX_TANKS];
static volatile int s_tankCount = 0;
static volatile uint32_t s_tankVer = 1;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ctxPgn = 0;
static N2kDevice s_dev[254];

void n2kSetPgnContext(uint32_t pgn) { s_ctxPgn = pgn; }

static const char *QTY_NAMES[Q_COUNT] = {
  "depth", "depth_offset", "stw", "sog", "cog", "sea_temp", "temp", "batt_v", "batt_a",
  "lat", "lon", "eng_rpm", "eng_oil_p", "eng_oil_t", "eng_coolant_t", "eng_alt_v",
  "eng_fuel_rate", "eng_hours", "eng_status1", "eng_status2", "tank_level", "tank_capacity"
};
static const char *QTY_UNITS[Q_COUNT] = {
  "m", "m", "kn", "kn", "deg", "C", "C", "V", "A",
  "deg", "deg", "rpm", "bar", "C", "C", "V",
  "L/h", "h", "", "", "%", "L"
};

static const char *QTY_LABELS[Q_COUNT] = {
  "Depth", "Depth offset", "Speed (log)", "SOG", "COG", "Sea temp", "Temperature", "Battery", "Battery current",
  "Latitude", "Longitude", "RPM", "Oil pressure", "Oil temp", "Coolant temp", "Alternator",
  "Fuel rate", "Engine hours", "Engine status 1", "Engine status 2", "Tank level", "Tank capacity"
};

const char *n2kQtyName(uint8_t q) { return q < Q_COUNT ? QTY_NAMES[q] : "?"; }
const char *n2kQtyLabel(uint8_t q) { return q < Q_COUNT ? QTY_LABELS[q] : "?"; }
int n2kQtyFromName(const char *name) {
  for (int i = 0; i < Q_COUNT; i++) if (strcmp(QTY_NAMES[i], name) == 0) return i;
  return -1;
}

struct PgnName { uint32_t pgn; const char *name; };
static const PgnName PGN_NAMES[] = {
  {59392, "ISO Acknowledge"}, {59904, "ISO Request"}, {60928, "Address claim"},
  {126208, "Group function"}, {126464, "PGN list"}, {126720, "Proprietary"},
  {126992, "System time"}, {126993, "Heartbeat"}, {126996, "Product info"}, {126998, "Config info"},
  {127233, "MOB"}, {127237, "Heading/track control"}, {127245, "Rudder"}, {127250, "Heading"},
  {127251, "Rate of turn"}, {127257, "Attitude"}, {127258, "Magnetic variation"},
  {127488, "Engine rapid"}, {127489, "Engine dynamic"}, {127493, "Transmission"}, {127497, "Trip fuel"},
  {127501, "Binary switch status"}, {127505, "Fluid level"}, {127506, "DC status"}, {127507, "Charger status"},
  {127508, "Battery status"}, {127513, "Battery config"},
  {128259, "Speed (water)"}, {128267, "Water depth"}, {128275, "Distance log"},
  {129025, "Position rapid"}, {129026, "COG & SOG rapid"}, {129029, "GNSS position"},
  {129033, "Time & date"}, {129038, "AIS class A pos"}, {129039, "AIS class B pos"},
  {129283, "Cross track error"}, {129284, "Navigation data"}, {129285, "Route/WP info"},
  {129539, "GNSS DOPs"}, {129540, "GNSS sats in view"}, {129794, "AIS class A static"},
  {129809, "AIS class B static A"}, {129810, "AIS class B static B"},
  {130306, "Wind"}, {130310, "Environment (outside)"}, {130311, "Environment"},
  {130312, "Temperature"}, {130313, "Humidity"}, {130314, "Pressure"}, {130316, "Temperature ext"},
  {130577, "Direction data"},
};
const char *n2kPgnName(uint32_t pgn) {
  for (const auto &p : PGN_NAMES) if (p.pgn == pgn) return p.name;
  if (pgn >= 65280 && pgn <= 65535) return "Proprietary";
  if (pgn >= 130816) return "Proprietary";
  return "";
}

// NMEA manufacturer codes (subset)
const char *n2kMfrName(uint16_t mfr) {
  switch (mfr) {
    case 135:  return "Airmar";
    case 137:  return "Maretron";
    case 174:  return "Volvo Penta";
    case 229:  return "Garmin";
    case 273:  return "Actisense";
    case 275:  return "Navico";
    case 358:  return "Victron";
    case 717:  return "Yacht Devices";
    case 1851: return "Raymarine";
    case 2046: return "DIY";
    default:   return nullptr;
  }
}

void n2kDeviceUpdate(uint8_t src, uint16_t mfr, uint8_t fn, uint8_t cls, const char *model) {
  if (src >= 254) return;
  portENTER_CRITICAL(&s_mux);
  N2kDevice &d = s_dev[src];
  d.mfr = mfr; d.devFunction = fn; d.devClass = cls; d.known = true;
  if (model && *model) strlcpy(d.model, model, sizeof(d.model));
  portEXIT_CRITICAL(&s_mux);
}

bool n2kDeviceCopy(uint8_t src, N2kDevice &out) {
  if (src >= 254) return false;
  portENTER_CRITICAL(&s_mux);
  out = s_dev[src];
  portEXIT_CRITICAL(&s_mux);
  return out.known;
}

void n2kDeviceLabel(uint8_t src, char *buf, size_t n) {
  N2kDevice d;
  if (!n2kDeviceCopy(src, d)) { snprintf(buf, n, "Device %u", src); return; }
  const char *m = n2kMfrName(d.mfr);
  if (d.model[0] && m)  snprintf(buf, n, "%s (%s)", d.model, m);
  else if (d.model[0])  snprintf(buf, n, "%s", d.model);
  else if (m)           snprintf(buf, n, "%s device", m);
  else                  snprintf(buf, n, "Mfr %u device", d.mfr);
}
const char *n2kQtyUnit(uint8_t q) { return q < Q_COUNT ? QTY_UNITS[q] : ""; }

static int findCh(uint8_t q, uint8_t inst, uint8_t sub) {
  for (int i = 0; i < s_chCount; i++)
    if (s_ch[i].qty == q && s_ch[i].instance == inst && s_ch[i].sub == sub) return i;
  return -1;
}

void n2kSet(N2kQty q, uint8_t inst, uint8_t sub, double v, uint8_t src) {
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  int i = findCh(q, inst, sub);
  if (i < 0 && s_chCount < N2K_MAX_CHANNELS) {
    i = s_chCount;
    s_ch[i].qty = q; s_ch[i].instance = inst; s_ch[i].sub = sub;
    s_chCount = i + 1;
  }
  if (i >= 0) {
    s_ch[i].pgn = s_ctxPgn;
    s_ch[i].value = v;
    s_ch[i].src = src;
    s_ch[i].lastMs = now;
  }
  portEXIT_CRITICAL(&s_mux);
#if N2K_DEBUG_SERIAL >= 1
  Serial.printf("[N2K] %s[%u/%u] = %.4f %s (src %u)\n", n2kQtyName(q), inst, sub, v, n2kQtyUnit(q), src);
#endif
}

bool n2kGet(N2kQty q, uint8_t inst, uint8_t sub, double &out, uint32_t maxAgeMs) {
  bool ok = false;
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  int i = findCh(q, inst, sub);
  if (i >= 0 && (now - s_ch[i].lastMs) <= maxAgeMs) { out = s_ch[i].value; ok = true; }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

bool n2kGetAny(N2kQty q, double &out, uint32_t maxAgeMs, uint8_t *inst, uint8_t *sub) {
  bool ok = false;
  uint32_t now = millis(), bestAge = UINT32_MAX;
  portENTER_CRITICAL(&s_mux);
  for (int i = 0; i < s_chCount; i++) {
    if (s_ch[i].qty != q) continue;
    uint32_t age = now - s_ch[i].lastMs;
    if (age <= maxAgeMs && age < bestAge) {
      bestAge = age; out = s_ch[i].value; ok = true;
      if (inst) *inst = s_ch[i].instance;
      if (sub)  *sub  = s_ch[i].sub;
    }
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

int n2kChannelCount() { return s_chCount; }

bool n2kChannelCopy(int i, N2kChannel &out) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (i >= 0 && i < s_chCount) { out = s_ch[i]; ok = true; }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

// ---------------------------------------------------------------- sniffer
void n2kNoteSeen(uint32_t pgn, uint8_t src) {
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  int i;
  for (i = 0; i < s_seenCount; i++)
    if (s_seen[i].pgn == pgn && s_seen[i].src == src) break;
  if (i == s_seenCount) {
    if (s_seenCount < N2K_MAX_SEEN_PGNS) {
      s_seenCount = s_seenCount + 1;
    } else {                               // full: reuse the entry heard from longest ago
      uint32_t oldest = 0; int oi = 0;
      for (int k = 0; k < s_seenCount; k++) {
        uint32_t age = now - s_seen[k].lastMs;
        if (age > oldest) { oldest = age; oi = k; }
      }
      i = oi;
    }
    s_seen[i].pgn = pgn; s_seen[i].src = src; s_seen[i].count = 0;
  }
  s_seen[i].count++;
  s_seen[i].lastMs = now;
  portEXIT_CRITICAL(&s_mux);
}

int n2kSeenCount() { return s_seenCount; }

bool n2kSeenCopy(int i, N2kSeenPgn &out) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (i >= 0 && i < s_seenCount) { out = s_seen[i]; ok = true; }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

// ---------------------------------------------------------------- tanks
static const char *FLUID_NAMES[] = {
  "Fuel", "Fresh water", "Grey water", "Live well", "Oil", "Black water", "Gasoline"
};

const char *n2kFluidName(uint8_t ft) { return ft < 7 ? FLUID_NAMES[ft] : "Tank"; }
bool n2kFluidIsWaste(uint8_t ft) { return ft == 2 || ft == 5; }

static int addTank(uint8_t ft, uint8_t inst, const char *defName, bool configured) {
  if (s_tankCount >= N2K_MAX_TANKS) return -1;
  int i = s_tankCount;
  N2kTank &t = s_tanks[i];
  t.fluidType = ft; t.instance = inst; t.configured = configured;
  if (defName) strlcpy(t.name, defName, sizeof(t.name));
  else snprintf(t.name, sizeof(t.name), "%s %u", n2kFluidName(ft), inst + 1);
  n2kSettingsApplyTankName(t);              // NVS override, if any
  __sync_synchronize();
  s_tankCount = i + 1;                      // publish only when fully written
  s_tankVer++;
  return i;
}

int n2kTankFindOrAdd(uint8_t ft, uint8_t inst) {
  for (int i = 0; i < s_tankCount; i++)
    if (s_tanks[i].fluidType == ft && s_tanks[i].instance == inst) return i;
  return addTank(ft, inst, nullptr, false);
}

int n2kTankCount() { return s_tankCount; }
N2kTank *n2kTankAt(int i) { return (i >= 0 && i < s_tankCount) ? &s_tanks[i] : nullptr; }
uint32_t n2kTankListVersion() { return s_tankVer; }
void n2kTankTouch() { s_tankVer++; }

void n2kDataInit() {
  s_chCount = 0; s_seenCount = 0; s_tankCount = 0;
  for (const auto &d : N2K_TANK_DEFS) addTank(d.fluidType, d.instance, d.name, true);
}
