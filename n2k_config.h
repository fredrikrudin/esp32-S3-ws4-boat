// n2k_config.h - NMEA 2000 configuration for esp32-S3-ws4-boat
// Everything you normally want to change for your own boat is in this file.
#pragma once
#include <Arduino.h>
#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// CAN / TWAI pins - Waveshare ESP32-S3-Touch-LCD-4
// Confirmed by Waveshare's V4.0 hardware reference: TWAI TX = GPIO6, RX = GPIO0.
// (I2C to touch/RTC/CH32V003 is GPIO15 SDA / GPIO7 SCL.)
// ---------------------------------------------------------------------------
#define N2K_CAN_TX_PIN        GPIO_NUM_6
#define N2K_CAN_RX_PIN        GPIO_NUM_0

// true  = listen only: never claims an address or sends PGNs (safest next to a Garmin network)
// false = joins the bus as a proper N2K display node (address claim, product info)
#define N2K_LISTEN_ONLY       true

// Run N2K parsing in its own FreeRTOS task (recommended - LVGL rendering in loop()
// can otherwise block long enough for the CAN receive queue to overflow).
#define N2K_OWN_TASK          true
#define N2K_TASK_CORE         0
#define N2K_RX_QUEUE_LEN      64

// 0 = quiet, 1 = print parsed values, 2 = also print every PGN/source received
#define N2K_DEBUG_SERIAL      0

// A value older than this is shown as "--"
#define N2K_STALE_MS          5000

#define N2K_MAX_CHANNELS      80
#define N2K_MAX_SEEN_PGNS     48
#define N2K_MAX_TANKS         12

// ---------------------------------------------------------------------------
// NOTE: warning/alarm limits and gauge ranges below are only FIRST-BOOT DEFAULTS.
// They can be changed per value (e.g. per engine or per tank) in the N2K
// settings tab or via the web API; changes are stored in NVS.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Engine
// N2K convention: engine instance 0 = port (or the only engine), 1 = starboard.
// These are only first-boot defaults; the choice is saved in NVS and can be
// changed from the Engine tab dropdown or via the web API.
// ---------------------------------------------------------------------------
#define ENG_DEFAULT_INSTANCE  0
#define ENG_DEFAULT_NAME      "Motor"
#define ENG_DEFAULT_SOURCE    255     // 255 = accept any source address

// Twin engines (start page shows both RPM dials): N2K instances and default names.
// Switched on in Settings -> NMEA 2000; names can be changed via the web API.
#define ENG_PORT_INSTANCE     0
#define ENG_STBD_INSTANCE     1
#define ENG_PORT_NAME         "Port"
#define ENG_STBD_NAME         "Starboard"

// Screensaver while the Engine tab is shown:
//   0 = normal screensaver
//   1 = never while the Engine tab is on screen (default)
//   2 = only blocked while the engine is running (RPM above ENG_RUNNING_RPM)
#define ENG_KEEP_AWAKE        1

#define ENG_RPM_MAX_SCALE     4000    // full scale of the RPM arc
#define ENG_RPM_RED_FROM      3600
#define ENG_RUNNING_RPM       400     // above this the engine counts as running
#define ENG_COOLANT_WARN_C    90
#define ENG_COOLANT_ALARM_C   98
#define ENG_OIL_TEMP_WARN_C   115
#define ENG_OIL_P_MIN_BAR     1.0     // alarm below this while running
#define ENG_ALT_V_LOW         13.0    // warn below this while running (12 V system)
#define ENG_ALT_V_HIGH        14.8    // warn above this

// Gauge scales on the Engine tab
#define ENG_COOLANT_SCALE_MIN 40
#define ENG_COOLANT_SCALE_MAX 120
#define ENG_OIL_P_SCALE_MAX   6       // bar
#define ENG_VOLT_SCALE_MIN    10
#define ENG_VOLT_SCALE_MAX    16

// ---------------------------------------------------------------------------
// Navigation tab
// ---------------------------------------------------------------------------
#define NAV_DEPTH_WARN_M      3.0
#define NAV_DEPTH_ALARM_M     1.5
#define NAV_BATT_INSTANCE     255     // 255 = first battery seen (e.g. the Garmin's supply volts)
#define NAV_BATT_LOW_V        12.0

// ---------------------------------------------------------------------------
// Tanks (PGN 127505 Fluid Level). All levels are shown in percent (0-100 %);
// litres are added when the sender also reports tank capacity.
//
// fluidType: 0 fuel, 1 fresh water, 2 grey water, 3 live well, 4 oil,
//            5 black water, 6 gasoline
//
// Tanks listed here always show (as "--" until data arrives). Tanks seen on the
// bus that are NOT listed are added automatically with a default name.
// Names can be overridden at runtime via the web API (stored in NVS).
//
// NOTE: LVGL's built-in Montserrat fonts have no å/ä/ö. Keep names ASCII unless
// you add a custom font with Latin-1 glyphs.
// ---------------------------------------------------------------------------
struct N2kTankDef { uint8_t fluidType; uint8_t instance; const char *name; };

static const N2kTankDef N2K_TANK_DEFS[] = {
  { 1, 0, "Fresh water" },
  { 2, 0, "Grey water"  },
  { 5, 0, "Black water" },
  // { 0, 0, "Diesel" },
};

#define TANK_WASTE_WARN_PCT   75      // grey/black: warn above
#define TANK_WASTE_ALARM_PCT  90
#define TANK_SUPPLY_WARN_PCT  25      // fresh/fuel: warn below
#define TANK_SUPPLY_ALARM_PCT 10
