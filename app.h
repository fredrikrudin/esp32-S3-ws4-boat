#pragma once
/*
 * esp32-S3-ws4-boat - shared declarations
 *
 * Every .cpp file includes this header. It holds the configuration,
 * the data types, the state shared between tasks, and the functions
 * each module offers to the others.
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <Preferences.h>
#include <string>
#include <math.h>
#include <time.h>
#include <FS.h>
#include "HWCDC.h"

/* ================================================================== */
/* Configuration                                                      */
/* ================================================================== */

/* Fonts: fall back gracefully if the big fonts are not enabled in lv_conf.h */
#if LV_FONT_MONTSERRAT_48
#define FONT_CLOCK (&lv_font_montserrat_48)
#elif LV_FONT_MONTSERRAT_40
#define FONT_CLOCK (&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_32
#define FONT_CLOCK (&lv_font_montserrat_32)
#else
#define FONT_CLOCK LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_32
#define FONT_BIG (&lv_font_montserrat_32)
#elif LV_FONT_MONTSERRAT_28
#define FONT_BIG (&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
#define FONT_BIG (&lv_font_montserrat_24)
#else
#define FONT_BIG LV_FONT_DEFAULT
#endif

#ifndef LV_KEYBOARD_CTRL_BTN_FLAGS
#define LV_KEYBOARD_CTRL_BTN_FLAGS (LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_CHECKED)
#endif

/* Big clock font for the screen saver: DejaVu Sans 96 px, digits, ':' and '-' only
   (font_clock_96.c, generated - the largest built-in LVGL font is 48 px) */
LV_FONT_DECLARE(font_clock_96)
#define FONT_SAVER (&font_clock_96)

#define DEG "\xC2\xB0"  // degree sign (UTF-8)
#define NO_NET_TEXT "Press Scan"
#define NO_NET_FOUND "No networks found"
#define KB_H 220  // on-screen keyboard height

#define MDNS_NAME "boat"        // web page at http://boat.local/
#define DEFAULT_WEB_NAME "Boat"  // title of the web page until renamed in Settings
#define SAVER_TIMEOUT_MS 30000  // screen saver after this long without touch
#define SAVER_COLOR 0x505050    // screen saver clock color (dim grey)

#define MAX_TAGS 8            // RuuviTags in range remembered at the same time
#define MAX_RUUVI 3           // RuuviTags that can be added and named
#define MAX_VIC 6             // Victron devices that can be added
#define MAX_VIC_SEEN 12       // Victron devices remembered for the "Add" list
#define VIC_STALE_MS 60000UL  // no Victron data for this long = "No signal"
#define HIST_HOURS 24         // hourly energy buckets kept
#define HIST_DAYS 7           // daily energy buckets kept

/* Victron Instant Readout record types */
#define VIC_SOLAR 0x01
#define VIC_BATTMON 0x02
#define VIC_INVERTER 0x03
#define VIC_DCDC 0x04
#define VIC_ACCHG 0x08

/* ================================================================== */
/* Data types                                                         */
/* ================================================================== */
struct Weather {
  bool valid = false;
  float temp = 0, feels = 0, wind = 0;
  int humidity = 0, code = 0;
  float today_max = 0, today_min = 0;
  char fc_day[3][8] = {};
  int fc_code[3] = {};
  float fc_max[3] = {}, fc_min[3] = {};
  time_t fetched = 0;
};

/* Settings and network results, protected by g_mtx */
struct Shared {
  // settings (stored in NVS)
  char ssid[33] = "";
  char pass[65] = "";
  char city[64] = "";
  char place[96] = "";
  float lat = 0, lon = 0;
  bool has_loc = false;
  int32_t utc_offset = 0;  // from Open-Meteo, follows DST
  bool offset_valid = false;
  char web_pass[33] = "";          // web page password, "" = no login
  char web_name[24] = DEFAULT_WEB_NAME;  // title of the web page
  // results for the UI
  char wifi_status[96] = "Not connected";
  bool status_changed = true;
  char scan_list[1024] = "";
  int scan_sel = 0;
  bool scan_ready = false;
  char loc_status[128] = "";
  bool loc_changed = true;
  Weather weather;
  bool weather_changed = false;
};

struct RuuviTag {
  bool used = false;
  char addr[18] = "";  // "AA:BB:CC:DD:EE:FF"
  float temp = 0, hum = 0, pres = 0;
  bool has_temp = false, has_hum = false, has_pres = false;
  int batt_mv = 0;
  int rssi = 0;
  uint32_t last_seen = 0;
};

/* RuuviTag: one entry per added tag, saved to flash as one block */
struct RuuviCfg {
  bool used;
  char mac[18];
  char name[20];
};

/* Victron: one entry per added device, saved to flash as one block */
struct VicCfg {
  bool used;
  uint8_t type;
  char mac[18];
  char name[24];
  uint8_t key[16];
};

/* Victron: every device heard on BLE (for the "Add" list) */
struct VicSeen {
  bool used;
  uint8_t type;
  char mac[18];
  int rssi;
  uint32_t last_seen;
};

/* Victron: decoded values, same index as VicCfg. NAN = not available */
struct VicData {
  uint32_t last_seen;  // 0 = never heard
  bool key_ok, key_bad;
  uint8_t type, state, error, aux_mode;
  int rssi;
  float batt_v, batt_i;   // battery side (solar, battery monitor, AC charger, inverter)
  float pv_w, yield_kwh;  // solar charger
  float soc, consumed_ah, aux_v, temp_c;
  int remaining_min;        // -1 = not available
  float in_v, out_v;        // DC-DC converter
  float ac_va, ac_v, ac_i;  // inverter AC output
  uint16_t alarm;           // inverter alarm bits
};

/* One period of energy history (an hour or a day) */
struct HistBucket {
  float solar_wh;
  float load_wh;
  float soc;  // state of charge at the end of the period, NAN if unknown
};

/* ================================================================== */
/* Shared state (state.cpp)                                           */
/* ================================================================== */
extern HWCDC USBSerial;  // defined in board.cpp

extern Shared g;
extern SemaphoreHandle_t g_mtx, ruuvi_mtx, vic_mtx;
extern Preferences prefs;

extern RuuviTag tags[MAX_TAGS];
extern RuuviCfg ruuvi_cfg[MAX_RUUVI];  // changed only by the UI (under g_mtx)
extern VicCfg vic_cfg[MAX_VIC];
extern VicData vic_data[MAX_VIC];
extern VicSeen vic_seen[MAX_VIC_SEEN];

/* Requests from the UI to the network task (which also does all flash writes) */
extern volatile bool cmd_scan, cmd_connect, cmd_geocode, cmd_weather;
extern volatile bool cmd_save_ruuvi, cmd_save_vic, cmd_save_scan, cmd_save_bl, cmd_save_web, cmd_save_feat;
extern volatile bool scan_restart;

/* Features that can be switched off in Settings (services stop too) */
extern volatile bool feat_ruuvi;   // Temp tab: RuuviTag reading
extern volatile bool feat_sdlog;   // write the log to the TF card (default off)
extern volatile bool feat_csv;     // write measurements to /data.csv (default off)
extern volatile uint8_t csv_interval_min;  // minutes between CSV lines

extern volatile uint8_t scan_interval_s;  // BLE scan interval, 1-10 s (1 = continuous)
extern volatile uint8_t bl_normal;        // backlight %, normal use
extern volatile uint8_t bl_saver;         // backlight %, screen saver

#define LOCK() xSemaphoreTake(g_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(g_mtx)

void state_init();  // mutexes etc., call before anything else uses the state
void load_cfg();    // settings from flash
void set_wifi_status(const char *fmt, ...);
void set_loc_status(const char *fmt, ...);
void mac_short(const char *addr, char *out);  // "AA:BB:CC:DD:EE:FF" -> "EEFF"
const char *wmo_text(int code);               // weather code -> text
String url_encode(const char *s);

/* ================================================================== */
/* Hardware, network, sensors                                         */
/* ================================================================== */
/* board.cpp */
void board_init();  // IO expander, touch, display, LVGL
void set_backlight(uint8_t pct);

/* net.cpp */
void net_start();                     // starts the network task (WiFi, NTP, weather, flash writes)
bool net_wait_wifi_init(uint32_t ms);  // waits until the WiFi driver is initialised

/* web.cpp */
void web_service();           // call from loop(): starts the web server once WiFi is up, then serves requests
void web_password_changed();  // call after g.web_pass changed: logs everyone out

/* ruuvi.cpp */
void ruuvi_parse(const std::string &md, const char *addr, int rssi);

/* victron.cpp */
const char *vic_type_name(uint8_t t);
const char *vic_state_name(uint8_t s);
const char *vic_alarm_text(uint16_t a);  // NULL = no alarm
bool vic_supported(uint8_t t);
bool vic_active_state(uint8_t s);  // charger/converter state that means energy flows
void vic_clear_data(VicData &v);
void vic_handle(const std::string &md, const char *addr, int rssi);
bool vic_fresh(const VicData &d, uint32_t now);
const char *vic_status(const VicData &d, uint8_t type, uint32_t now);

/* ble.cpp */
void ble_start();
void ble_scan_service();          // call from loop()
void ble_pause_scan(bool pause);  // stop scanning while connecting to a device

/* sdlog.cpp (log to Serial, to a ring buffer readable at /log, and to the TF card) */
void log_begin();
void logf(const char *fmt, ...);
void log_dump(String &out);
bool sd_log_mount();
void sd_log_unmount();
bool sd_log_ok();
const char *sd_log_status();
size_t sd_log_size();
const char *sd_log_name();
void sd_card_info(char *out, size_t n);
bool sd_log_new_file();
int sd_log_delete_old();
void sd_list_files(String &out);
fs::FS &sd_fs();  // the mounted card (SD mode or SPI mode)

/* history.cpp (energy history behind the Power tab's History screen) */
void history_begin();
void history_service();  // call from loop()
void history_get(HistBucket **hour_arr, HistBucket **day_arr);
void history_totals(bool daily, float *solar_kwh, float *load_kwh);

/* ui_history.cpp */
void build_history_screen();
void show_history_screen();
void history_timer_cb(lv_timer_t *t);

/* datalog.cpp (measurements as CSV, settings backup/restore) */
void csv_service();  // call from loop()
const char *csv_status();
bool settings_backup();
bool settings_restore();

/* ================================================================== */
/* UI                                                                 */
/* ================================================================== */
/* ui_common.cpp */
extern lv_obj_t *tab_home, *tab_nav, *tab_engine, *tab_tanks, *tab_power, *tab_temp, *tab_weather, *tab_settings, *kb;
void build_ui();
void ui_update_tabs();  // hides the tabs of switched-off features
void kb_show(lv_obj_t *ta);
void kb_hide();
lv_obj_t *make_row(lv_obj_t *parent, lv_flex_align_t main_align);
lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb);
lv_obj_t *make_ta(lv_obj_t *parent, const char *placeholder, void (*on_ready)() = nullptr, bool hex = false);
lv_obj_t *make_heading(lv_obj_t *parent, const char *txt);
lv_obj_t *make_section(lv_obj_t *parent, const char *title);  // card with a heading, for Settings
lv_obj_t *make_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb);
lv_obj_t *make_grey_label(lv_obj_t *parent);
lv_obj_t *make_slider(lv_obj_t *parent, int min, int max, int val, lv_event_cb_t cb);
void set_label(lv_obj_t *l, const char *txt);  // only redraws when the text changes
lv_obj_t *make_segment(lv_obj_t *parent, lv_event_cb_t cb, void *user_data);  // Off | On control
void set_segment(lv_obj_t *seg, bool on);

/* ui_home.cpp */
void build_home_tab();
void home_timer_cb(lv_timer_t *t);

/* ui_power.cpp */
void build_power_tab();
void power_timer_cb(lv_timer_t *t);

/* ui_temp.cpp */
void build_temp_tab();
void ruuvi_timer_cb(lv_timer_t *t);

/* ui_weather.cpp */
void build_weather_tab();
void format_local_time(char *tb, char *db);  // tb >= 16, db >= 64 chars
void clock_timer_cb(lv_timer_t *t);
void net_poll_cb(lv_timer_t *t);

/* ui_settings.cpp */
extern lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;
void build_settings_tab();
void ruuvi_settings_refresh(const RuuviTag *copy, uint32_t now);

/* ui_victron_settings.cpp */
void settings_victron(lv_obj_t *parent);
void victron_settings_refresh(const VicCfg *cfg, const VicData *dat, const VicSeen *seen, uint32_t now);

/* NMEA 2000 (n2k_*.cpp, ui_nav/engine/tanks/n2k_settings.cpp) - see N2K_INTEGRATION.md */
void n2k_save_pending();  // called by the network task: does the N2K flash writes

/* ui_saver.cpp */
void build_saver();
void saver_timer_cb(lv_timer_t *t);
