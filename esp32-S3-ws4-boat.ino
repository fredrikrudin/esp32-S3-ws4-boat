/*
 * esp32-S3-ws4-boat
 * Boat display for the Waveshare ESP32-S3-Touch-LCD-4 (V4, CH32V003 IO expander).
 *
 * Tabs: Home | Nav (NMEA 2000) | Engine (N2K) | Tanks (N2K) | Power (Victron BLE) |
 *       Temp (RuuviTag) | Weather | Settings (N2K devices and limits open from here)
 * Web page: http://boat.local/ (JSON at /json, NMEA 2000 at /api/n2k)
 * See README.md for libraries and Arduino IDE settings.
 */

#include "app.h"
#include "n2k_bus.h"

void setup() {
  USBSerial.begin(115200);
  USBSerial.printf("Internal heap at start: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  board_init();  // IO expander, touch, display, LVGL
  log_begin();   // Serial + ring buffer (+ TF card when switched on)
  history_begin();
  state_init();
  load_cfg();    // loads feat_sdlog / feat_csv, so the card is mounted after this
  if (feat_sdlog || feat_csv) sd_log_mount();  // before WiFi and Bluetooth, as in Waveshare's SD demo
  n2kInit();   // NMEA 2000 settings, limits and value store (the tabs read them)

  build_ui();
  set_backlight(bl_normal);
  USBSerial.printf("Internal heap after UI: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  /* WiFi first: its buffers must be in DMA-capable internal RAM, and Bluetooth
     would otherwise take that memory before WiFi gets a chance. */
  net_start();
  if (!net_wait_wifi_init(5000)) USBSerial.println("WiFi init did not finish in time");
  USBSerial.printf("Internal heap after WiFi init: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  n2kStart();  // NMEA 2000 CAN bus + receive task (core 0); after WiFi, like Bluetooth
  ble_start();
  USBSerial.printf("Setup done. Internal heap free %u, largest block %u\n",
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void loop() {
  ble_scan_service();
  web_service();
  csv_service();
  history_service();
  lv_timer_handler();
  delay(5);
}
