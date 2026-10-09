<!-- esp32-S3-ws4-boat v1.0 -->
# Changelog

The version is set by `FW_VERSION` in `app.h` and shown in Settings → Device → About,
on the web page and in the first log line at every start. Every file carries it on its
first line; `python3 tools/set_version.py <version>` changes it everywhere. Every change
gets an entry, newest first.

## 1.0 — 2026-10-08 — first release

Brought in step with [esp32-S3-ws4-caravan](https://github.com/fredrikrudin/esp32-S3-ws4-caravan) 1.0:
the shared parts are now the same code, with the boat's NMEA 2000 screens on top.

New from the caravan display:

- **Language**: English (default) or Swedish, under Settings → Device → Language. Built in,
  as gettext `.po` files in `lang/` (edit with Poedit, `tools/i18n.py` builds them). Every
  screen is translated, the NMEA 2000 tabs, gauges, alarm texts, PGN names and the default
  engine and tank names included; Å, Ä and Ö show through `font_latin1.c`.
- **Ruuvi tab** (was Temp): each tag has a chart of its daily low and high over the last
  7 days, kept in flash.
- **Settings in four pages**: Connect, Sensors, NMEA 2000, Device.
- **Alarms** under Settings → Device: battery warning and alarm levels (were fixed at 20 %
  and 10 %), wind warning, SD card warning, and whether a new alarm wakes the screen. The
  start page banner now also shows Victron devices with a wrong key or gone quiet, inverter
  alarms, the battery running empty, strong wind, WiFi lost and the board's own battery, and
  the web page lists them at the top.
- **WiFi indicator** on the start page: the WiFi symbol with signal bars.
- **Debug** under Settings → Device: the **Serial monitor** switch (off by default: only
  faults go to USB serial) and the performance log. `N2K_DEBUG_SERIAL` output follows it.
- **Power**: slows the CPU while the screen sleeps; optional battery mode on the onboard LiPo
  with a shutdown threshold; keep-alive for the SW6106 power bank chip where fitted.
- **Starting screen** with your own text, size, colour and time ("BOAT" by default).
- **Weather** every hour while the screen is awake (was every 15 minutes, always), and a
  **Refresh** button.
- **Web page**: can be switched off; sent in pieces (less memory); charts of the last
  24 hours and 10 minutes; board battery; warnings.
- Tabs that are not on screen are no longer redrawn (Nav, Engine, Tanks too).
- History chart fixes (axis labels, totals that fit).
- Settings backup also holds the alarms, starting screen, power and debug settings and the
  language.
- Version 1.0 on the first line of every file, `tools/set_version.py`, CC BY-NC 4.0
  license (`LICENSE.md`), screenshots rendered by the real UI code (`tools/screenshots`).

Fixes to the first boat build:

- It did not compile: the start page used an undeclared variable for the depth, and the
  NMEA 2000 status text in Settings had line breaks inside a string.
- `n2k_bus_test.ino` was in the sketch folder as well as in `tools/`, so the Arduino IDE
  built it into the firmware (a second `setup()`). It is now only in `tools/n2k_bus_test/`.
- The gauge scales showed their numbers multiplied by 100 (oil pressure 0-600, coolant
  4000-12000): LVGL 8.4 gives the tick label buffer a length of 0.
- Full reset now also erases the NMEA 2000 settings and limits.
- The Engine tab's presets are stored in English and shown translated; "Babord" and
  "Styrbord" saved earlier are still recognised. The default engine name is "Engine"
  ("Motor" in Swedish).

## Before 1.0

### First build — 2026-09-28
- The boat display, derived from the caravan display: NMEA 2000 over the on-board CAN
  transceiver (start page, Nav, Engine and Tanks tabs, device browser, gauge ranges and
  limits, twin engines, web API), with Victron, RuuviTag, weather and SD card logging.
