<!-- esp32-S3-ws4-boat v1.0 -->
# Screenshots

The images in the main README are rendered by the firmware's own UI code: the `ui_*.cpp`
files, the language files and fonts and the NMEA 2000 value store, compiled for a PC with
LVGL 8.4 and given example data (an engine, a depth sounder, a Garmin plotter, four tanks). What you see is what the board draws, pixel for pixel, apart from the clock and
date, which come from the PC.

```
sh tools/screenshots/build.sh
```

The first run downloads LVGL 8.4 and builds it (a minute or two). The images end up in
`tools/screenshots/out_en/` and `out_sv/`, with `sheet.png` showing them all. Each language is shot three
times: normally, with alarms (shallow water, hot engine, full black water tank, low battery,
strong wind) and with twin engines. Copy the ones you want into `screens/`.

| File | What it is |
|---|---|
| `shots.cpp` | Stand-ins for the hardware, the example data, and which screens are shot |
| `stub/` | Small versions of the Arduino, WiFi, SD and ArduinoJson headers, just enough to compile the UI |
| `topng.py` | Turns the raw frames into PNG files (needs numpy and Pillow) |
| `build.sh` | All of the above in one go |

Change the example data in `example_data()` in `shots.cpp`.
