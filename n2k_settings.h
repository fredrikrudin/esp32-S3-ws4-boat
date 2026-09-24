// n2k_settings.h - persistent N2K settings (NVS namespace "n2k")
#pragma once
#include <Arduino.h>
#include "n2k_data.h"

struct N2kSettings {
  uint8_t engInstance;      // N2K engine instance shown on the Engine tab (0 = port/single, 1 = stbd)
  uint8_t engSource;        // N2K source address filter, 255 = any
  char    engName[24];      // display name, e.g. "Babord", "Styrbord", "Motor"
};

extern N2kSettings n2kSettings;

void     n2kSettingsLoad();
uint32_t n2kSettingsVersion();     // changes on every save (UI re-reads)
void     n2kSetEngine(uint8_t instance, const char *name, uint8_t source);
void     n2kSetTankName(uint8_t fluidType, uint8_t instance, const char *name);
void     n2kSettingsApplyTankName(N2kTank &t);   // used internally when a tank is added
