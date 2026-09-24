# NMEA 2000 in esp32-S3-ws4-boat

How the NMEA 2000 part works: the on-board CAN transceiver, the PGNs that are
decoded, the tabs that show them, and the settings and web API around them.

| File | Purpose |
|---|---|
| `n2k_config.h` | Pins, alarm limits, tank list, defaults. **Edit this one.** |
| `n2k_bus.cpp/.h` | TWAI/CAN init and PGN handlers |
| `n2k_data.cpp/.h` | Generic value store (quantity + instance + sub), tank list, PGN sniffer |
| `n2k_settings.cpp/.h` | Engine instance/name/source and tank names in NVS |
| `n2k_limits.cpp/.h` | Gauge ranges and warn/alarm limits per value (defaults + NVS overrides) |
| `n2k_web.cpp` | `n2kJson()`, `n2kApplySetting()`, CSV columns for `/data.csv` |
| `ui_n2k_common.cpp/.h` | Shared LVGL tile widget, fonts, colours |
| `ui_gauge.cpp/.h` | Generic analog gauge: needle, tick scale, coloured zones, digital readout |
| `ui_nav.cpp/.h` | Navigation tab: depth, log speed, SOG/COG, sea temp, volts, position |
| `ui_engine.cpp/.h` | Engine tab: big tachometer, coolant / oil pressure / alternator gauges, oil temp, fuel rate, hours, engine status |
| `ui_n2k_settings.cpp/.h` | Device/PGN browser (opened from Settings): tap a value to edit its gauge range and limits |
| `ui_tanks.cpp/.h` | Tanks tab: vertical card gauges for fresh/grey/black water (and any other tank on the bus) in % |

## How it fits into the firmware

- `n2kInit()` (setup, before the UI) loads the engine choice, tank names, limits and
  prepares the value store. `n2kStart()` (after WiFi init, before Bluetooth) opens the CAN
  bus and starts the receive task on core 0.
- The Nav, Engine and Tanks tabs are built in `ui_common.cpp` and start their own
  LVGL timers. The device/PGN browser opens from **Settings → NMEA 2000**.
- Flash writes: changes made on the display or through the web API are kept in RAM and
  written by the network task (`n2k_save_pending()` in `net.cpp`), like every other
  setting in this project - flash writes can disturb the RGB display.
- **Settings → SD card → Back up settings** includes the N2K settings and limits.
- The CSV log (`/data.csv`) gets the N2K columns after the Victron/Ruuvi/weather columns.

## Web API

| Endpoint | Purpose |
|---|---|
| `GET /api/n2k` | Devices → PGNs → values with limits, tanks, engine settings |
| `POST /api/n2k/set` | `key` + `value`, see below |

The same login applies as for the rest of the web page (`?key=<password>` for scripts).

Settings keys:

| key | value | example |
|---|---|---|
| `engine_instance` | 0–252 | `1` |
| `engine_name` | text | `Styrbord` |
| `engine_source` | N2K source address, 255 = any | `23` |
| `tank_name` | `fluidType,instance,name` | `1,0,Fresh water fwd` |
| `limits` | `q,inst,sub,gauge_min,gauge_max,alarm_low,warn_low,warn_high,alarm_high` (empty = off) | `eng_coolant_t,0,0,40,120,,,90,98` |
| `limits_reset` | `q,inst,sub` | `eng_coolant_t,0,0` |

Example with curl:
```
curl -X POST http://boat.local/api/n2k/set -d "key=engine_name&value=Styrbord"
```

## Gauge ranges and limits

Each value has a gauge range (min/max) and optional limits:
alarm low ≤ warn low < warn high ≤ alarm high. They drive the gauge zones
(red | amber | green | amber | red), the colour of tiles and tank cards, and the
tile/rim alarm colours. Limits belong to the value (quantity + instance + sub), e.g.
"coolant of engine #1" or "black water tank #0", not to a source address, so they
survive a device getting a new N2K address.

On the touch screen: **Settings → NMEA 2000 → Devices, gauge ranges and limits**, find the
device and PGN, tap the value (⚙ icon; orange = custom), edit, **Save**. **Defaults**
reverts to `n2k_config.h`. Low oil pressure and low alternator voltage only count while
the engine runs.

Device names come from address claim / product info. In listen-only mode a device
only shows its model name if it announced itself after the display booted (most do
at power-up); otherwise it is listed by manufacturer or as "Device <src>".

## Twin engines (two displays)

N2K convention: engine instance **0 = port (babord) or single**, **1 = starboard (styrbord)**.
Pick the preset in the Engine tab dropdown on each display, or set it via the web API.
The choice is stored in flash.

If both engine gateways report instance 0 (happens with some aftermarket gateways),
set `engine_source` on each display to the source address of its gateway (listed per
device in `/api/n2k` and in the Settings → NMEA 2000 browser). Engine PGNs from other sources are then ignored on that display.

## Adding more N2K data

1. Add a quantity to `N2kQty` in `n2k_data.h` (and its name/unit in `n2k_data.cpp`).
2. Add the PGN to `kRxPgns` and a `case` in `onMsg()` in `n2k_bus.cpp` that calls `n2kSet()`.
3. Read it anywhere with `n2kGet(Q_..., instance, sub, value)`.

Tanks need no code: any PGN 127505 fluid level appears on the Tanks tab automatically.

## Hardware notes

- CAN pins: TX = GPIO6, RX = GPIO0 (Waveshare's V4.0 hardware reference). They don't
  collide with the display, touch/I2C (GPIO15/7) or SD card (GPIO2/1/4).
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
