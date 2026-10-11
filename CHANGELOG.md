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
- **Power**: optional battery mode on the onboard LiPo
  with a shutdown threshold; keep-alive for the SW6106 power bank chip where fitted.
- **Starting screen** with your own text, size, colour and time ("BOAT" by default).
- **Weather** every hour while the screen is awake (was every 15 minutes, always), and a
  **Refresh** button.
- **Web page**: can be switched off; sent in pieces (less memory); charts of the last
  24 hours and 10 minutes; board battery; warnings.
- The CPU is no longer slowed to 80 MHz under the screen saver or in battery mode, and
  the "Slow the CPU while the screen sleeps" switch is gone: the board froze (display and
  web page) soon after slowing down. The screen saver still dims the backlight.
- **The web server is new** (`web_http.cpp`), because the page loaded once and then
  stopped answering, and could freeze the display. Arduino's WebServer serves one
  connection at a time and waits on it: a browser's spare connection that sends nothing
  held it for 5 s, and a phone that went to sleep halfway through a reply for 10 s per
  piece. The new one handles six connections at once, only ever sends what the network
  takes without waiting, and drops connections that stall. When lwIP is short of memory
  for a moment it tries again instead of giving up (the first version did, and the page
  arrived cut off after 1436 bytes), and it lets the browser close the connection, so
  nothing still on its way is lost.
- **The web page is new**: one fixed file (`web_page.h`) that loads `/json` every 5 s and
  draws itself, so the board builds only a few kB of JSON per update instead of the whole
  page. It adapts to phone, tablet and PC, says when the board doesn't answer and keeps the
  last values, and pauses while the browser tab is hidden. `/json` has the charts' history
  and more fields; the old fields are unchanged. `tools/webpreview` runs it on a PC.
- Web page layout: cards fill the gaps in the grid instead of leaving holes, "Last 10
  minutes" takes the full width only with two or more charts.
- The web page follows the display's language (Settings → Device → Language): its texts
  come from the new `/lang` and the same `.po` files as the display (`tools/i18n.py` finds
  the page's `T("...")` texts), and `/json` sends the date, weather, Victron types and
  states and schedule days in it. The login, file list and log pages are translated too.
  Before, the page was English with a Swedish date and warnings.
- A watchdog: if the main loop (screen, web page and most services) stops for 30 s, the
  board restarts, and the first log lines after the restart say where it was stuck. A
  crash, brown-out or hardware watchdog reset is logged at start-up too.
- Renaming the web page (Settings → Connect → Web page) now also changes its address:
  "Cabby 620" is at `http://cabby-620.local/`, straight away and without a restart.
- The web page keeps working while the screen saver shows.
- WiFi modem sleep stays on. Turning it off while the web server ran (for faster `.local`
  answers) made the page load only now and then: WiFi and Bluetooth share the antenna, and
  without modem sleep Bluetooth cuts into WiFi traffic and packets are lost. Espressif calls
  modem sleep the essential prerequisite for running both.
- The web server keeps 12 kB of internal RAM free for WiFi: only ~35 kB is free with the
  display, WiFi and Bluetooth running, and replies sent at full speed could use the rest.
  It now sends two segments at a time, waits while memory is short, lets new connections
  wait while all six are busy (they got an error page before), and writes a line to the
  log once a minute when anything went wrong: stalled replies, send errors, or time spent
  waiting for memory or a free connection.
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
