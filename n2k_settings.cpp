// n2k_settings.cpp
#include <Preferences.h>
#include "n2k_settings.h"
#include "n2k_limits.h"
#include "n2k_config.h"

N2kSettings n2kSettings;
static volatile uint32_t s_ver = 1;
static volatile bool s_dirtyEngine = false, s_dirtyTanks = false;

// Tank names chosen by the user, kept as one block in flash
struct N2kTankNameRec { uint8_t ft, inst, used, pad; char name[24]; };
static N2kTankNameRec s_tn[N2K_MAX_TANKS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t n2kSettingsVersion() { return s_ver; }

void n2kSettingsLoad() {
  Preferences p;
  p.begin("n2k", false);
  n2kSettings.engInstance = p.getUChar("engInst", ENG_DEFAULT_INSTANCE);
  n2kSettings.engSource   = p.getUChar("engSrc", ENG_DEFAULT_SOURCE);
  String nm = p.getString("engName", ENG_DEFAULT_NAME);
  n2kSettings.twin = p.getBool("twin", false);
  String pn = p.getString("portName", ENG_PORT_NAME);
  String sn = p.getString("stbdName", ENG_STBD_NAME);
  strlcpy(n2kSettings.portName, pn.c_str(), sizeof(n2kSettings.portName));
  strlcpy(n2kSettings.stbdName, sn.c_str(), sizeof(n2kSettings.stbdName));
  memset(s_tn, 0, sizeof(s_tn));
  if (p.getBytesLength("tnames") == sizeof(s_tn)) p.getBytes("tnames", s_tn, sizeof(s_tn));
  p.end();
  strlcpy(n2kSettings.engName, nm.c_str(), sizeof(n2kSettings.engName));
  s_ver++;
}

void n2kSetEngine(uint8_t instance, const char *name, uint8_t source) {
  n2kSettings.engInstance = instance;
  n2kSettings.engSource   = source;
  if (name && *name) strlcpy(n2kSettings.engName, name, sizeof(n2kSettings.engName));
  s_dirtyEngine = true;
  s_ver++;
}

void n2kSetTwin(bool twin, const char *portName, const char *stbdName) {
  n2kSettings.twin = twin;
  if (portName && *portName) strlcpy(n2kSettings.portName, portName, sizeof(n2kSettings.portName));
  if (stbdName && *stbdName) strlcpy(n2kSettings.stbdName, stbdName, sizeof(n2kSettings.stbdName));
  s_dirtyEngine = true;
  s_ver++;
}

void n2kSetTankName(uint8_t ft, uint8_t inst, const char *name) {
  if (!name || !*name) return;
  int i = n2kTankFindOrAdd(ft, inst);
  N2kTank *t = n2kTankAt(i);
  if (t) strlcpy(t->name, name, sizeof(t->name));

  portENTER_CRITICAL(&s_mux);
  int slot = -1;
  for (int k = 0; k < N2K_MAX_TANKS; k++)
    if (s_tn[k].used && s_tn[k].ft == ft && s_tn[k].inst == inst) { slot = k; break; }
  if (slot < 0) for (int k = 0; k < N2K_MAX_TANKS; k++) if (!s_tn[k].used) { slot = k; break; }
  if (slot >= 0) {
    s_tn[slot].ft = ft; s_tn[slot].inst = inst; s_tn[slot].used = 1;
    strlcpy(s_tn[slot].name, name, sizeof(s_tn[slot].name));
  }
  portEXIT_CRITICAL(&s_mux);
  s_dirtyTanks = true;
  n2kTankTouch();
}

void n2kSettingsApplyTankName(N2kTank &t) {
  portENTER_CRITICAL(&s_mux);
  for (int k = 0; k < N2K_MAX_TANKS; k++)
    if (s_tn[k].used && s_tn[k].ft == t.fluidType && s_tn[k].inst == t.instance) {
      strlcpy(t.name, s_tn[k].name, sizeof(t.name));
      break;
    }
  portEXIT_CRITICAL(&s_mux);
}

// Called from the network task (net.cpp save_pending)
void n2k_save_pending() {
  if (s_dirtyEngine || s_dirtyTanks) {
    Preferences p;
    p.begin("n2k", false);
    if (s_dirtyEngine) {
      s_dirtyEngine = false;
      p.putUChar("engInst", n2kSettings.engInstance);
      p.putUChar("engSrc",  n2kSettings.engSource);
      p.putString("engName", n2kSettings.engName);
      p.putBool("twin", n2kSettings.twin);
      p.putString("portName", n2kSettings.portName);
      p.putString("stbdName", n2kSettings.stbdName);
    }
    if (s_dirtyTanks) {
      s_dirtyTanks = false;
      N2kTankNameRec copy[N2K_MAX_TANKS];
      portENTER_CRITICAL(&s_mux);
      memcpy(copy, s_tn, sizeof(copy));
      portEXIT_CRITICAL(&s_mux);
      p.putBytes("tnames", copy, sizeof(copy));
    }
    p.end();
  }
  n2kLimitsSavePending();
}

// ------------------------------------------------------------------ backup / restore
static String toHex(const void *d, size_t n) {
  static const char *H = "0123456789abcdef";
  const uint8_t *b = (const uint8_t *)d;
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 15]; }
  return s;
}

static bool fromHex(const char *s, void *out, size_t n) {
  if (!s || strlen(s) != n * 2) return false;
  uint8_t *o = (uint8_t *)out;
  for (size_t i = 0; i < n; i++) {
    char b[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char *end;
    o[i] = (uint8_t)strtoul(b, &end, 16);
    if (*end) return false;
  }
  return true;
}

void n2kBackup(JsonDocument &doc) {
  JsonObject n = doc["n2k"].to<JsonObject>();
  n["eng_inst"] = n2kSettings.engInstance;
  n["eng_src"]  = n2kSettings.engSource;
  n["eng_name"] = n2kSettings.engName;
  n["twin"]     = n2kSettings.twin;
  n["port_name"] = n2kSettings.portName;
  n["stbd_name"] = n2kSettings.stbdName;
  portENTER_CRITICAL(&s_mux);
  N2kTankNameRec copy[N2K_MAX_TANKS];
  memcpy(copy, s_tn, sizeof(copy));
  portEXIT_CRITICAL(&s_mux);
  n["tanks"] = toHex(copy, sizeof(copy));
  size_t len;
  const void *lim = n2kLimitsBlob(&len);
  n["limits"] = toHex(lim, len);
}

void n2kRestore(JsonDocument &doc) {
  JsonObject n = doc["n2k"];
  if (n.isNull()) return;
  Preferences p;
  p.begin("n2k", false);
  p.putUChar("engInst", n["eng_inst"] | ENG_DEFAULT_INSTANCE);
  p.putUChar("engSrc",  n["eng_src"]  | ENG_DEFAULT_SOURCE);
  p.putString("engName", (const char *)(n["eng_name"] | ENG_DEFAULT_NAME));
  p.putBool("twin", n["twin"] | false);
  p.putString("portName", (const char *)(n["port_name"] | ENG_PORT_NAME));
  p.putString("stbdName", (const char *)(n["stbd_name"] | ENG_STBD_NAME));
  static N2kTankNameRec tn[N2K_MAX_TANKS];
  if (fromHex(n["tanks"] | "", tn, sizeof(tn))) p.putBytes("tnames", tn, sizeof(tn));
  p.end();

  size_t len;
  n2kLimitsBlob(&len);
  uint8_t *buf = (uint8_t *)malloc(len);
  if (buf && fromHex(n["limits"] | "", buf, len)) n2kLimitsWriteBlob(buf, len);
  free(buf);
}
