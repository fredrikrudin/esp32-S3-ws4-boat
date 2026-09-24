// n2k_data.h - generic, thread-safe store for values received from NMEA 2000
//
// Every value is a "channel" identified by (quantity, instance, sub):
//   quantity : what it is (depth, engine rpm, tank level ...)
//   instance : N2K instance (engine 0/1, tank 0..n, battery 0..n)
//   sub      : extra key, e.g. fluid type for tanks or temperature source
// Channels are created automatically the first time a value arrives, so new
// sensors on the bus show up without code changes (see n2kJson()).
#pragma once
#include <Arduino.h>
#include "n2k_config.h"

enum N2kQty : uint8_t {
  Q_DEPTH,          // m below transducer
  Q_DEPTH_OFFSET,   // m (+ = transducer to waterline, - = transducer to keel)
  Q_STW,            // kn speed through water
  Q_SOG,            // kn
  Q_COG,            // deg true
  Q_SEA_TEMP,       // C
  Q_TEMP,           // C other temperatures, sub = N2K temperature source
  Q_BATT_V,         // V
  Q_BATT_A,         // A
  Q_LAT,            // deg
  Q_LON,            // deg
  Q_ENG_RPM,        // rpm
  Q_ENG_OIL_P,      // bar
  Q_ENG_OIL_T,      // C
  Q_ENG_COOL_T,     // C
  Q_ENG_ALT_V,      // V
  Q_ENG_FUEL_RATE,  // L/h
  Q_ENG_HOURS,      // h
  Q_ENG_STATUS1,    // bit field (PGN 127489 discrete status 1)
  Q_ENG_STATUS2,    // bit field (PGN 127489 discrete status 2)
  Q_TANK_LEVEL,     // %  , sub = fluid type
  Q_TANK_CAP,       // L  , sub = fluid type
  Q_COUNT
};

struct N2kChannel {
  uint8_t  qty, instance, sub, src;
  uint32_t pgn;             // PGN that last delivered this value
  double   value;
  uint32_t lastMs;
};

void        n2kDataInit();
void        n2kSetPgnContext(uint32_t pgn);   // called by the bus before handling each message
void        n2kSet(N2kQty q, uint8_t inst, uint8_t sub, double v, uint8_t src);
bool        n2kGet(N2kQty q, uint8_t inst, uint8_t sub, double &out, uint32_t maxAgeMs = N2K_STALE_MS);
// Freshest value of a quantity regardless of instance/sub
bool        n2kGetAny(N2kQty q, double &out, uint32_t maxAgeMs = N2K_STALE_MS,
                      uint8_t *inst = nullptr, uint8_t *sub = nullptr);
int         n2kChannelCount();
bool        n2kChannelCopy(int i, N2kChannel &out);
const char *n2kQtyName(uint8_t q);
const char *n2kQtyUnit(uint8_t q);
const char *n2kQtyLabel(uint8_t q);          // human readable, e.g. "Coolant temp"
int         n2kQtyFromName(const char *name); // "eng_coolant_t" -> Q_ENG_COOL_T, -1 if unknown
const char *n2kPgnName(uint32_t pgn);         // "Water depth", or "" if unknown

// ---- Devices (source addresses) with names from address claim / product info
struct N2kDevice { uint16_t mfr; uint8_t devFunction, devClass; char model[33]; bool known; };
void        n2kDeviceUpdate(uint8_t src, uint16_t mfr, uint8_t fn, uint8_t cls, const char *model);
bool        n2kDeviceCopy(uint8_t src, N2kDevice &out);
const char *n2kMfrName(uint16_t mfr);
void        n2kDeviceLabel(uint8_t src, char *buf, size_t n);   // "GPSMAP 922 (Garmin)" / "Device 35"

// ---- PGN sniffer: every PGN/source seen on the bus (for setup & debugging)
struct N2kSeenPgn { uint32_t pgn; uint8_t src; uint32_t count; uint32_t lastMs; };
void n2kNoteSeen(uint32_t pgn, uint8_t src);
int  n2kSeenCount();
bool n2kSeenCopy(int i, N2kSeenPgn &out);

// ---- Tanks
struct N2kTank { uint8_t fluidType; uint8_t instance; char name[24]; bool configured; };
int         n2kTankCount();
N2kTank    *n2kTankAt(int i);
int         n2kTankFindOrAdd(uint8_t fluidType, uint8_t instance);
const char *n2kFluidName(uint8_t fluidType);
bool        n2kFluidIsWaste(uint8_t fluidType);
uint32_t    n2kTankListVersion();   // changes when tanks are added/renamed
void        n2kTankTouch();         // bump version (after rename)
