// n2k_settings.h - persistent N2K settings (NVS namespace "n2k")
//
// Changes are made in RAM and written to flash by the network task
// (n2k_save_pending(), called from net.cpp), like all other settings in this
// project: flash writes can disturb the RGB display, so they happen in one place.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "n2k_data.h"

struct N2kSettings {
  uint8_t engInstance;      // N2K engine instance shown on the Engine tab (0 = port/single, 1 = stbd)
  uint8_t engSource;        // N2K source address filter, 255 = any
  char    engName[24];      // display name, e.g. "Babord", "Styrbord", "Motor"
  bool    twin;             // two engines: the start page shows port and starboard RPM
  char    portName[16];     // name on the port dial (instance ENG_PORT_INSTANCE)
  char    stbdName[16];     // name on the starboard dial (instance ENG_STBD_INSTANCE)
};

extern N2kSettings n2kSettings;

void     n2kSettingsLoad();
uint32_t n2kSettingsVersion();     // changes on every change (UI re-reads)
void     n2kSetEngine(uint8_t instance, const char *name, uint8_t source);
void     n2kSetTwin(bool twin, const char *portName, const char *stbdName);   // NULL name = keep
void     n2kSetTankName(uint8_t fluidType, uint8_t instance, const char *name);
void     n2kSettingsApplyTankName(N2kTank &t);   // used internally when a tank is added

// Settings backup (datalog.cpp): adds / reads an "n2k" object in the backup JSON.
// Restore writes straight to flash; the board restarts afterwards.
void     n2kBackup(JsonDocument &doc);
void     n2kRestore(JsonDocument &doc);
