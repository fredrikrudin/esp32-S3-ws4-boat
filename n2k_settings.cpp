// n2k_settings.cpp
#include <Preferences.h>
#include "n2k_settings.h"
#include "n2k_config.h"

N2kSettings n2kSettings;
static volatile uint32_t s_ver = 1;

uint32_t n2kSettingsVersion() { return s_ver; }

void n2kSettingsLoad() {
  Preferences p;
  p.begin("n2k", false);
  n2kSettings.engInstance = p.getUChar("engInst", ENG_DEFAULT_INSTANCE);
  n2kSettings.engSource   = p.getUChar("engSrc", ENG_DEFAULT_SOURCE);
  String nm = p.getString("engName", ENG_DEFAULT_NAME);
  p.end();
  strlcpy(n2kSettings.engName, nm.c_str(), sizeof(n2kSettings.engName));
  s_ver++;
}

void n2kSetEngine(uint8_t instance, const char *name, uint8_t source) {
  n2kSettings.engInstance = instance;
  n2kSettings.engSource   = source;
  if (name && *name) strlcpy(n2kSettings.engName, name, sizeof(n2kSettings.engName));
  Preferences p;
  p.begin("n2k", false);
  p.putUChar("engInst", n2kSettings.engInstance);
  p.putUChar("engSrc",  n2kSettings.engSource);
  p.putString("engName", n2kSettings.engName);
  p.end();
  s_ver++;
}

static void tankKey(char *k, size_t n, uint8_t ft, uint8_t inst) {
  snprintf(k, n, "tn%u_%u", ft, inst);     // NVS keys max 15 chars
}

void n2kSetTankName(uint8_t ft, uint8_t inst, const char *name) {
  int i = n2kTankFindOrAdd(ft, inst);
  N2kTank *t = n2kTankAt(i);
  if (!t || !name) return;
  strlcpy(t->name, name, sizeof(t->name));
  char k[16]; tankKey(k, sizeof(k), ft, inst);
  Preferences p;
  p.begin("n2k", false);
  p.putString(k, t->name);
  p.end();
  n2kTankTouch();
}

void n2kSettingsApplyTankName(N2kTank &t) {
  char k[16]; tankKey(k, sizeof(k), t.fluidType, t.instance);
  Preferences p;
  p.begin("n2k", true);
  if (p.isKey(k)) {
    String s = p.getString(k, "");
    if (s.length()) strlcpy(t.name, s.c_str(), sizeof(t.name));
  }
  p.end();
}
