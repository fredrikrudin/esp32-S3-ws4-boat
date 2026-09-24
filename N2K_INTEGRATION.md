# NMEA 2000 for esp32-S3-ws4-boat

These files add NMEA 2000 (via the on-board CAN transceiver) to a copy of the
caravan firmware. They are self-contained: Victron, RuuviTag, weather and SD
code from the caravan project stays as it is.

| File | Purpose |
|---|---|
| `n2k_config.h` | Pins, alarm limits, tank list, defaults. **Edit this one.** |
| `n2k_bus.cpp/.h` | TWAI/CAN init and PGN handlers |
| `n2k_data.cpp/.h` | Generic value store (quantity + instance + sub), tank list, PGN sniffer |
| `n2k_settings.cpp/.h` | Engine instance/name/source and tank names in NVS |
| `n2k_limits.cpp/.h` | Gauge ranges and warn/alarm limits per value (defaults + NVS overrides) |
| `n2k_web.cpp` | `n2kJson()`, `n2kApplySetting()`, CSV helpers for SD logging |
| `ui_n2k_common.cpp/.h` | Shared LVGL tile widget, fonts, colours |
| `ui_gauge.cpp/.h` | Generic analog gauge: needle, tick scale, coloured zones, digital readout |
| `ui_nav.cpp/.h` | Navigation tab: depth, log speed, SOG/COG, sea temp, volts, position |
| `ui_engine.cpp/.h` | Engine tab: big tachometer, coolant / oil pressure / alternator gauges, oil temp, fuel rate, hours, engine status |
| `ui_n2k_settings.cpp/.h` | N2K settings tab: devices → PGNs → values, tap to edit gauge range and limits |
| `ui_tanks.cpp/.h` | Tanks tab: vertical card gauges for fresh/grey/black water (and any other tank on the bus) in % |

## Libraries

1. **NMEA2000** by Timo Lappalainen (Library Manager: "NMEA2000-library", or ZIP from github.com/ttlappalainen/NMEA2000)
2. **NMEA2000_esp32_twai** by Sergei Podshivalov – ZIP from github.com/sergei/NMEA2000_esp32_twai
   (the older `NMEA2000_esp32` does not build for ESP32-S3)

UI code is written for **LVGL 8.x** (as in the Waveshare demos). For bigger numbers enable
`LV_FONT_MONTSERRAT_48`, `_28`, `_20` and `_14` in `lv_conf.h`; the code falls back to the
default font if they are off. The gauges use `lv_meter` (`LV_USE_METER 1`, on by default in LVGL 8).

## Hooking it into the main sketch

```cpp
#include "n2k_bus.h"
#include "ui_nav.h"
#include "ui_engine.h"
#include "ui_tanks.h"
#include "ui_n2k_settings.h"

void setup() {
  // ... existing init (display, LVGL, BLE, WiFi, SD) ...
  n2kBegin();

  // where the other tabs are created (inside the LVGL lock if the project uses one):
  ui_nav_create   (lv_tabview_add_tab(tabview, "Nav"));
  ui_engine_create(lv_tabview_add_tab(tabview, "Engine"));
  ui_tanks_create (lv_tabview_add_tab(tabview, "Tanks"));
  ui_n2k_settings_create(lv_tabview_add_tab(tabview, "N2K"));   // or a page inside your Settings tab
}

void loop() {
  n2kLoop();          // no-op when N2K_OWN_TASK is true (default)
  // ... existing loop ...
}
```

Each tab starts its own `lv_timer`, so updates happen in LVGL context with no extra wiring.

## Screensaver

While the Engine tab is on screen, the screensaver is blocked (`ENG_KEEP_AWAKE` in
`n2k_config.h`: 0 = off, 1 = whenever the Engine tab is shown, 2 = only while the engine runs).

- If the screensaver uses `lv_disp_get_inactive_time()`, nothing else is needed - the
  Engine tab resets LVGL's inactivity timer every 250 ms.
- If it keeps its own timer (e.g. `lastTouchMs`), add one check:

```cpp
#include "ui_engine.h"
if (ui_engine_keep_awake()) lastTouchMs = millis();   // before the timeout test
```

## Web API (works with WebServer or AsyncWebServer)

```cpp
server.on("/api/n2k", HTTP_GET, []() {
  server.send(200, "application/json", n2kJson());
});
server.on("/api/n2k/set", HTTP_POST, []() {
  bool ok = n2kApplySetting(server.arg("key"), server.arg("value"));
  server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "bad key/value");
});
```

Settings keys:

| key | value | example |
|---|---|---|
| `engine_instance` | 0–252 | `1` |
| `engine_name` | text | `Styrbord` |
| `engine_source` | N2K source address, 255 = any | `23` |
| `tank_name` | `fluidType,instance,name` | `1,0,Fresh water fwd` |
| `limits` | `q,inst,sub,gauge_min,gauge_max,alarm_low,warn_low,warn_high,alarm_high` (empty = off) | `eng_coolant_t,0,0,40,120,,,90,98` |
| `limits_reset` | `q,inst,sub` | `eng_coolant_t,0,0` |

`/api/n2k` returns `devices` → `pgns` → `values`: every device (source address, with
model name when the device has announced it) with the PGNs it sends, message counts,
the values decoded from each PGN and their current limits. Use it to see what the
Garmin 922 and other devices actually send, and which source address each engine
gateway has.

## Gauge ranges and limits

Each value has a gauge range (min/max) and optional limits:
alarm low ≤ warn low < warn high ≤ alarm high. They drive the gauge zones
(red | amber | green | amber | red), the colour of tiles and tank cards, and the
tile/rim alarm colours. Limits belong to the value (quantity + instance + sub), e.g.
"coolant of engine #1" or "black water tank #0", not to a source address, so they
survive a device getting a new N2K address.

On the touch screen: open the **N2K** tab, find the device and PGN, tap the value
(⚙ icon; orange = custom), edit, **Save**. **Defaults** reverts to `n2k_config.h`.
Low oil pressure and low alternator voltage only count while the engine runs.

Device names come from address claim / product info. In listen-only mode a device
only shows its model name if it announced itself after the display booted (most do
at power-up); otherwise it is listed by manufacturer or as "Device <src>".

## SD logging

```cpp
file.println(n2kCsvHeader());   // once per file
file.println(n2kCsvLine());     // e.g. every 10 s, prefix your own timestamp
```

## Twin engines (two displays)

N2K convention: engine instance **0 = port (babord) or single**, **1 = starboard (styrbord)**.
Pick the preset in the Engine tab dropdown on each display, or set it via the web API.
The choice is stored in NVS.

If both engine gateways report instance 0 (happens with some aftermarket gateways),
set `engine_source` on each display to the source address of its gateway (see `pgns`
in `/api/n2k`). Engine PGNs from other sources are then ignored on that display.

## Adding more N2K data

1. Add a quantity to `N2kQty` in `n2k_data.h` (and its name/unit in `n2k_data.cpp`).
2. Add the PGN to `kRxPgns` and a `case` in `onMsg()` in `n2k_bus.cpp` that calls `n2kSet()`.
3. Read it anywhere with `n2kGet(Q_..., instance, sub, value)`.

Tanks need no code: any PGN 127505 fluid level appears on the Tanks tab automatically.

## Hardware notes

- CAN pins default to TX = GPIO6, RX = GPIO0 (Waveshare's CAN demo for this board).
  Check the V4 schematic before connecting.
- Do **not** switch on the board's 120 Ω CAN termination – an N2K backbone is already
  terminated at both ends.
- Connect NET-H/NET-L to CAN-H/CAN-L and share ground with the boat 12 V system the
  board is powered from.
- The CAN controller runs in TWAI normal mode, so the display ACKs frames even when
  listen-only. This matters on a small bus: with the TWAI controller itself in listen-only
  mode, a sender whose only partner is this display gets no ACK and keeps retrying.
- `N2K_LISTEN_ONLY true` (default): the display never transmits PGNs or claims an address.
  Set it to `false` if you want it to appear as a device in the Garmin device list.
- Set `N2K_DEBUG_SERIAL 2` in `n2k_config.h` for a first test – every received PGN and
  source is printed to Serial.

## PGNs handled

| PGN | Data |
|---|---|
| 127488 | Engine RPM |
| 127489 | Oil pressure, oil temp, coolant temp, alternator V, fuel rate, hours, alarm bits |
| 127505 | Tank levels (%) and capacity (L) |
| 127508 | Battery volts/amps |
| 128259 | Speed through water |
| 128267 | Depth + transducer offset |
| 129025 | Position |
| 129026 | COG / SOG |
| 130310, 130311, 130312, 130316 | Sea water temperature (and other temperatures) |
