// n2k_limits.h - gauge ranges and warning/alarm limits per value, editable at runtime
//
// Limits are keyed by (quantity, instance, sub) - e.g. coolant temp of engine 0 or
// the level of fresh water tank 0 - not by source address, so they survive a device
// getting a new N2K address. Defaults come from n2k_config.h; changes are stored in NVS.
#pragma once
#include <Arduino.h>
#include <math.h>
#include "n2k_data.h"

struct N2kLimits {
  float gMin, gMax;          // gauge range
  float alarmLo, warnLo;     // NAN = not used
  float warnHi, alarmHi;     // NAN = not used
};

enum N2kLevel : uint8_t { N2K_LVL_OK, N2K_LVL_WARN, N2K_LVL_ALARM };

#define N2K_MAX_LIMIT_OVERRIDES 48

void      n2kLimitsInit();                                            // load overrides from NVS
bool      n2kQtyHasLimits(uint8_t q);                                 // false for position, status bits ...
N2kLimits n2kLimitsDefault(uint8_t q, uint8_t sub);
N2kLimits n2kLimitsGet(uint8_t q, uint8_t inst, uint8_t sub);         // override or default
bool      n2kLimitsIsCustom(uint8_t q, uint8_t inst, uint8_t sub);
bool      n2kLimitsSet(uint8_t q, uint8_t inst, uint8_t sub, const N2kLimits &l);   // false if full
void      n2kLimitsReset(uint8_t q, uint8_t inst, uint8_t sub);
uint32_t  n2kLimitsVersion();                                         // changes on every set/reset

// checkLow = false ignores the low limits (e.g. oil pressure while the engine is stopped)
N2kLevel  n2kLimitsEval(const N2kLimits &l, double v, bool checkLow = true);

// Order check: gMin < gMax, alarmLo <= warnLo < warnHi <= alarmHi (unused ones skipped)
bool      n2kLimitsValid(const N2kLimits &l, const char **err);
