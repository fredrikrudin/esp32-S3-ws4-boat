/* Settings tab: WiFi, web page password, weather location, RuuviTags, NMEA 2000,
   display, sensor scan interval, SD card. The Victron section lives in its own file. */
#include "app.h"
#include "n2k_bus.h"
#include "n2k_data.h"
#include "n2k_settings.h"
#include "ui_n2k_settings.h"

lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;  // also updated by net_poll_cb()

static lv_obj_t *ta_pass, *ta_city;
static lv_obj_t *ta_webpass, *ta_webname, *lbl_web;
static lv_obj_t *dd_ruuvi, *ruuvi_list, *lbl_ruuvi_msg;
static lv_obj_t *ruuvi_editor, *lbl_ruuvi_edit_title, *ta_rname;
static lv_obj_t *ruuvi_list_lbl[MAX_RUUVI];
static int ruuvi_edit_idx = -1;  // slot being edited, -1 = new tag
static char ruuvi_edit_mac[18];
static lv_obj_t *sl_bl, *lbl_bl, *sl_bl_saver, *lbl_bl_saver;
static lv_obj_t *lbl_scan;
static lv_obj_t *lbl_n2k;
static lv_obj_t *lbl_sdlog, *lbl_csv, *dd_csv;
static char dd_addr[MAX_TAGS][18];  // MAC for each row of the Ruuvi "Add" list
static int dd_count = 0;

/* ---------- WiFi ---------- */
static void connect_now() {
  char ssid[33];
  lv_dropdown_get_selected_str(dd_ssid, ssid, sizeof(ssid));
  if (!ssid[0] || !strcmp(ssid, NO_NET_TEXT) || !strcmp(ssid, NO_NET_FOUND)) {
    lv_label_set_text(lbl_wifi_status, "Scan and choose a network first");
    return;
  }
  LOCK();
  strlcpy(g.ssid, ssid, sizeof(g.ssid));
  strlcpy(g.pass, lv_textarea_get_text(ta_pass), sizeof(g.pass));
  UNLOCK();
  cmd_connect = true;
  kb_hide();
}

static void scan_btn_cb(lv_event_t *e) {
  cmd_scan = true;
}
static void connect_btn_cb(lv_event_t *e) {
  connect_now();
}

/* ---------- Web page ---------- */
static void update_web_label() {
  bool has_pass;
  LOCK();
  has_pass = g.web_pass[0] != 0;
  UNLOCK();
  char name[24];
  LOCK();
  strlcpy(name, g.web_name, sizeof(name));
  UNLOCK();
  lv_label_set_text_fmt(lbl_web, "\"%s\" at http://%s.local/ on the same WiFi.\n%s", name, MDNS_NAME,
                        has_pass ? "Password required." : "No password: anyone on the WiFi can view the page.");
}

static void webpass_save_now() {
  LOCK();
  strlcpy(g.web_pass, lv_textarea_get_text(ta_webpass), sizeof(g.web_pass));
  const char *n = lv_textarea_get_text(ta_webname);
  strlcpy(g.web_name, n[0] ? n : DEFAULT_WEB_NAME, sizeof(g.web_name));
  UNLOCK();
  cmd_save_web = true;
  web_password_changed();  // logs everyone out, so the new password applies at once
  update_web_label();
  kb_hide();
}

static void webpass_save_cb(lv_event_t *e) {
  webpass_save_now();
}

/* ---------- Location ---------- */
static void locate_now() {
  const char *c = lv_textarea_get_text(ta_city);
  if (!c[0]) return;
  LOCK();
  strlcpy(g.city, c, sizeof(g.city));
  UNLOCK();
  cmd_geocode = true;
  kb_hide();
}

static void locate_btn_cb(lv_event_t *e) {
  locate_now();
}

/* ---------- RuuviTags (up to MAX_RUUVI, with names) ---------- */
static void ruuvi_close_editor() {
  lv_obj_add_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  ruuvi_edit_idx = -1;
  kb_hide();
}

static void ruuvi_open_editor(const char *mac, int slot) {
  ruuvi_edit_idx = slot;
  strlcpy(ruuvi_edit_mac, mac, sizeof(ruuvi_edit_mac));
  char s[8], name[20];
  mac_short(mac, s);
  lv_label_set_text_fmt(lbl_ruuvi_edit_title, "Ruuvi %s  (%s)", s, mac);
  if (slot >= 0) strlcpy(name, ruuvi_cfg[slot].name, sizeof(name));
  else snprintf(name, sizeof(name), "Ruuvi %s", s);
  lv_textarea_set_text(ta_rname, name);
  lv_label_set_text(lbl_ruuvi_msg, "");
  lv_obj_clear_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(tab_settings);
  lv_obj_scroll_to_view_recursive(ruuvi_editor, LV_ANIM_ON);
}

static void ruuvi_list_btn_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  ruuvi_open_editor(ruuvi_cfg[i].mac, i);
}

static void ruuvi_rebuild_list() {
  lv_obj_clean(ruuvi_list);
  int count = 0;
  for (int i = 0; i < MAX_RUUVI; i++) {
    ruuvi_list_lbl[i] = NULL;
    if (!ruuvi_cfg[i].used) continue;
    lv_obj_t *b = lv_btn_create(ruuvi_list);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_add_event_cb(b, ruuvi_list_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_text(l, ruuvi_cfg[i].name);
    ruuvi_list_lbl[i] = l;
    count++;
  }
  if (!count) {
    lv_obj_t *l = make_grey_label(ruuvi_list);
    lv_label_set_text(l, "No tags added yet");
  }
}

static void ruuvi_save_now() {
  const char *name = lv_textarea_get_text(ta_rname);
  int slot = ruuvi_edit_idx;
  if (slot < 0) {
    for (int i = 0; i < MAX_RUUVI; i++) {
      if (!ruuvi_cfg[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    lv_label_set_text_fmt(lbl_ruuvi_msg, "Max %d tags. Delete one first.", MAX_RUUVI);
    return;
  }
  char s[8];
  mac_short(ruuvi_edit_mac, s);
  LOCK();
  RuuviCfg &c = ruuvi_cfg[slot];
  c.used = true;
  strlcpy(c.mac, ruuvi_edit_mac, sizeof(c.mac));
  if (name[0]) strlcpy(c.name, name, sizeof(c.name));
  else snprintf(c.name, sizeof(c.name), "Ruuvi %s", s);
  UNLOCK();
  cmd_save_ruuvi = true;
  ruuvi_close_editor();
  ruuvi_rebuild_list();
}

static void ruuvi_save_cb(lv_event_t *e) {
  ruuvi_save_now();
}

static void ruuvi_delete_cb(lv_event_t *e) {
  if (ruuvi_edit_idx >= 0) {
    LOCK();
    memset(&ruuvi_cfg[ruuvi_edit_idx], 0, sizeof(RuuviCfg));
    UNLOCK();
    cmd_save_ruuvi = true;
  }
  ruuvi_close_editor();
  ruuvi_rebuild_list();
}

static void ruuvi_cancel_cb(lv_event_t *e) {
  ruuvi_close_editor();
}

static void ruuvi_add_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_ruuvi);
  if (i >= dd_count) return;  // "Searching..." row
  int used = 0;
  for (int k = 0; k < MAX_RUUVI; k++) used += ruuvi_cfg[k].used;
  if (used >= MAX_RUUVI) {
    lv_label_set_text_fmt(lbl_ruuvi_msg, "Max %d tags. Tap one above and delete it first.", MAX_RUUVI);
    return;
  }
  ruuvi_open_editor(dd_addr[i], -1);
}

static int ruuvi_cfg_index(const char *mac) {
  for (int i = 0; i < MAX_RUUVI; i++)
    if (ruuvi_cfg[i].used && !strcmp(ruuvi_cfg[i].mac, mac)) return i;
  return -1;
}

/* Called once per second by ruuvi_timer_cb() with a copy of the tags in range */
void ruuvi_settings_refresh(const RuuviTag *copy, uint32_t now) {
  char b[96];

  /* added tags: live temperature */
  for (int i = 0; i < MAX_RUUVI; i++) {
    if (!ruuvi_cfg[i].used || !ruuvi_list_lbl[i]) continue;
    const RuuviTag *t = NULL;
    for (int k = 0; k < MAX_TAGS; k++)
      if (copy[k].used && !strcmp(copy[k].addr, ruuvi_cfg[i].mac)) t = &copy[k];
    char s[8];
    mac_short(ruuvi_cfg[i].mac, s);
    if (t && t->has_temp && now - t->last_seen < 10UL * 60 * 1000)
      snprintf(b, sizeof(b), "%s\nRuuvi %s  -  %.1f" DEG "C", ruuvi_cfg[i].name, s, t->temp);
    else
      snprintf(b, sizeof(b), "%s\nRuuvi %s  -  not heard", ruuvi_cfg[i].name, s);
    set_label(ruuvi_list_lbl[i], b);
  }

  /* tags in range that aren't added yet (don't change the list under the user's finger) */
  if (lv_dropdown_is_open(dd_ruuvi)) return;
  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_ruuvi);
  if (ci < dd_count) strlcpy(cur, dd_addr[ci], sizeof(cur));

  static char last_opts[MAX_TAGS * 40] = "";
  char opts[MAX_TAGS * 40] = "";
  int count = 0, cur_idx = 0;
  for (int i = 0; i < MAX_TAGS; i++) {
    if (!copy[i].used || now - copy[i].last_seen > 10UL * 60 * 1000) continue;  // gone > 10 min
    if (ruuvi_cfg_index(copy[i].addr) >= 0) continue;                          // already added
    char s[8], line[40];
    mac_short(copy[i].addr, s);
    if (copy[i].has_temp) snprintf(line, sizeof(line), "%sRuuvi %s   %.1f" DEG, count ? "\n" : "", s, copy[i].temp);
    else snprintf(line, sizeof(line), "%sRuuvi %s", count ? "\n" : "", s);
    strlcat(opts, line, sizeof(opts));
    strlcpy(dd_addr[count], copy[i].addr, sizeof(dd_addr[count]));
    if (!strcmp(copy[i].addr, cur)) cur_idx = count;
    count++;
  }
  dd_count = count;
  if (!count) strcpy(opts, "Searching...");
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_ruuvi, opts);
    lv_dropdown_set_selected(dd_ruuvi, cur_idx);
  }
}

/* ---------- Display ---------- */
static void update_bl_labels() {
  lv_label_set_text_fmt(lbl_bl, "Brightness: %d%%", bl_normal);
  if (bl_saver == 0) lv_label_set_text(lbl_bl_saver, "Screen saver brightness: off");
  else lv_label_set_text_fmt(lbl_bl_saver, "Screen saver brightness: %d%%", bl_saver);
}

static void bl_slider_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *sl = lv_event_get_target(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (sl == sl_bl) bl_normal = lv_slider_get_value(sl);
    else bl_saver = lv_slider_get_value(sl);
    update_bl_labels();
    set_backlight(sl == sl_bl ? bl_normal : bl_saver);  // live preview while dragging
  } else if (code == LV_EVENT_RELEASED) {
    set_backlight(bl_normal);  // back to normal after previewing the screen saver level
    cmd_save_bl = true;
  }
}

/* ---------- Sensor scan interval ---------- */
static void update_scan_label() {
  if (scan_interval_s <= 1) lv_label_set_text(lbl_scan, "Update sensors: continuously (1 s)");
  else lv_label_set_text_fmt(lbl_scan, "Update sensors: every %d s", scan_interval_s);
}

static void scan_slider_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    scan_interval_s = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    update_scan_label();
  } else if (code == LV_EVENT_RELEASED) {
    scan_restart = true;   // apply the new interval
    cmd_save_scan = true;  // save once, when the finger lifts
  }
}

/* ---------- NMEA 2000 ---------- */
static void n2k_open_cb(lv_event_t *e) {
  kb_hide();
  ui_n2k_show_screen();
}

static void twin_cb(lv_event_t *e) {
  n2kSetTwin(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED), nullptr, nullptr);
}

static void n2k_timer_cb(lv_timer_t *t) {
  if (!lv_obj_is_visible(lbl_n2k)) return;
  int devs = 0;
  N2kSeenPgn sp;
  uint8_t seen[64];
  for (int i = 0; i < n2kSeenCount(); i++) {
    if (!n2kSeenCopy(i, sp)) continue;
    bool have = false;
    for (int k = 0; k < devs; k++) have |= seen[k] == sp.src;
    if (!have && devs < 64) seen[devs++] = sp.src;
  }
  char b[160];
  snprintf(b, sizeof(b), "%s
%lu messages, %d devices, %d PGNs
Engine tab: %s (instance %u)",
           n2kBusOk() ? LV_SYMBOL_OK " Receiving" : LV_SYMBOL_WARNING " No NMEA 2000 data",
           (unsigned long)n2kMsgCount(), devs, n2kSeenCount(), n2kSettings.engName, n2kSettings.engInstance);
  set_label(lbl_n2k, b);
}

/* ---------- SD card and logging ---------- */
static void update_sdlog_label() {
  char info[64];
  sd_card_info(info, sizeof(info));
  if (sd_log_ok())
    lv_label_set_text_fmt(lbl_sdlog, "%s\n%s, %u kB", info, sd_log_name(), (unsigned)(sd_log_size() / 1024));
  else
    lv_label_set_text_fmt(lbl_sdlog, "%s\n%s", sd_log_status(),
                          feat_sdlog ? "Insert a card and tap Mount." : "The log is kept in memory and readable at /log.");
}

static void sd_mount_cb(lv_event_t *e) {
  if (sd_log_ok()) {
    lv_label_set_text(lbl_sdlog, "Already mounted");
  } else if (sd_log_mount()) {
    logf("TF card mounted");
  }
  update_sdlog_label();
}

static void sd_eject_cb(lv_event_t *e) {
  sd_log_unmount();  // flushes and closes the file first
  update_sdlog_label();
}

static void sd_newfile_cb(lv_event_t *e) {
  if (sd_log_new_file()) logf("New log file started");
  update_sdlog_label();
}

static void sd_delete_cb(lv_event_t *e) {
  int n = sd_log_delete_old();
  lv_label_set_text_fmt(lbl_sdlog, "Deleted %d old log file(s)", n);
}

static void sdlog_cb(lv_event_t *e) {
  feat_sdlog = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  if (feat_sdlog) {
    sd_log_mount();
    logf("Logging to the TF card switched on");
  } else if (!feat_csv) {
    sd_log_unmount();  // keep the card mounted while the CSV log still uses it
  }
  update_sdlog_label();
}

static void sd_refresh_cb(lv_event_t *e) {
  update_sdlog_label();
}

static void update_csv_label() {
  lv_label_set_text_fmt(lbl_csv, "%s", feat_csv ? csv_status() : "Measurements are not being logged.");
}

static void csv_cb(lv_event_t *e) {
  feat_csv = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  if (feat_csv) sd_log_mount();
  update_csv_label();
}

static void csv_interval_cb(lv_event_t *e) {
  const uint8_t mins[] = { 1, 5, 15, 60 };
  csv_interval_min = mins[lv_dropdown_get_selected(dd_csv)];
  cmd_save_feat = true;
}

static void backup_cb(lv_event_t *e) {
  if (!sd_log_ok()) sd_log_mount();
  lv_label_set_text(lbl_csv, settings_backup() ? "Settings saved to /settings.json" : "Backup failed - is a card inserted?");
}

static void restore_cb(lv_event_t *e) {
  if (!sd_log_ok()) sd_log_mount();
  if (settings_restore()) {
    lv_label_set_text(lbl_csv, "Settings restored - restarting...");
    lv_refr_now(NULL);
    delay(1500);
    ESP.restart();
  } else {
    lv_label_set_text(lbl_csv, "Restore failed - no /settings.json on the card?");
  }
}

/* ---------- feature switches ---------- */
static void feat_ruuvi_cb(lv_event_t *e) {
  feat_ruuvi = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  ui_update_tabs();
}

/* ---------- build ---------- */
void build_settings_tab() {
  lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab_settings, 16, 0);  // space between the section cards
  lv_obj_set_style_pad_bottom(tab_settings, 16, 0);

  /* ---- WiFi ---- */
  lv_obj_t *sec = make_section(tab_settings, LV_SYMBOL_WIFI "  WiFi");

  lv_obj_t *row = make_row(sec, LV_FLEX_ALIGN_START);
  dd_ssid = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ssid, 1);
  lv_dropdown_set_options(dd_ssid, g.ssid[0] ? g.ssid : NO_NET_TEXT);
  make_btn(row, LV_SYMBOL_REFRESH " Scan", scan_btn_cb);

  ta_pass = make_ta(sec, "Password", connect_now);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_set_width(ta_pass, LV_PCT(100));
  lv_textarea_set_text(ta_pass, g.pass);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_OK " Connect", connect_btn_cb);
  lbl_wifi_status = lv_label_create(row);
  lv_obj_set_flex_grow(lbl_wifi_status, 1);
  lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_wifi_status, "");

  /* ---- Web page ---- */
  sec = make_section(tab_settings, LV_SYMBOL_EYE_OPEN "  Web page");
  lbl_web = lv_label_create(sec);
  lv_obj_set_width(lbl_web, LV_PCT(100));
  lv_label_set_long_mode(lbl_web, LV_LABEL_LONG_WRAP);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_webname = make_ta(row, "Name of the page", webpass_save_now);
  lv_textarea_set_max_length(ta_webname, 23);
  lv_obj_set_flex_grow(ta_webname, 1);
  lv_textarea_set_text(ta_webname, g.web_name);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_webpass = make_ta(row, "Password (empty = no login)", webpass_save_now);
  lv_textarea_set_password_mode(ta_webpass, true);
  lv_textarea_set_max_length(ta_webpass, 32);
  lv_obj_set_flex_grow(ta_webpass, 1);
  lv_textarea_set_text(ta_webpass, g.web_pass);
  make_btn(row, LV_SYMBOL_SAVE " Save", webpass_save_cb);
  update_web_label();

  /* ---- Weather ---- */
  sec = make_section(tab_settings, LV_SYMBOL_GPS "  Weather location");
  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_city = make_ta(row, "City (or City, Country)", locate_now);
  lv_obj_set_flex_grow(ta_city, 1);
  lv_textarea_set_text(ta_city, g.city);
  make_btn(row, LV_SYMBOL_OK " Set", locate_btn_cb);
  lbl_loc = lv_label_create(sec);
  lv_obj_set_width(lbl_loc, LV_PCT(100));
  lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_loc, "");

  /* ---- Temperature: RuuviTags ---- */
  sec = make_section(tab_settings, LV_SYMBOL_BLUETOOTH "  Temperature (RuuviTags)");
  make_switch_row(sec, "Read RuuviTags", feat_ruuvi, feat_ruuvi_cb);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  dd_ruuvi = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ruuvi, 1);
  lv_dropdown_set_options(dd_ruuvi, "Searching...");
  make_btn(row, LV_SYMBOL_PLUS " Add", ruuvi_add_cb);

  ruuvi_list = lv_obj_create(sec);
  lv_obj_remove_style_all(ruuvi_list);
  lv_obj_set_size(ruuvi_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ruuvi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ruuvi_list, 6, 0);
  lv_obj_clear_flag(ruuvi_list, LV_OBJ_FLAG_SCROLLABLE);

  ruuvi_editor = lv_obj_create(sec);
  lv_obj_set_size(ruuvi_editor, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ruuvi_editor, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ruuvi_editor, 8, 0);
  lv_obj_clear_flag(ruuvi_editor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  lbl_ruuvi_edit_title = lv_label_create(ruuvi_editor);
  ta_rname = make_ta(ruuvi_editor, "Name, e.g. Inside", ruuvi_save_now);
  lv_obj_set_width(ta_rname, LV_PCT(100));
  lv_textarea_set_max_length(ta_rname, 19);
  row = make_row(ruuvi_editor, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Save", ruuvi_save_cb);
  lv_obj_t *del = make_btn(row, LV_SYMBOL_TRASH " Delete", ruuvi_delete_cb);
  lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);
  make_btn(row, "Cancel", ruuvi_cancel_cb);

  lbl_ruuvi_msg = lv_label_create(sec);
  lv_obj_set_width(lbl_ruuvi_msg, LV_PCT(100));
  lv_label_set_long_mode(lbl_ruuvi_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl_ruuvi_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_ruuvi_msg, "");
  ruuvi_rebuild_list();

  /* ---- NMEA 2000 ---- */
  sec = make_section(tab_settings, LV_SYMBOL_SHUFFLE "  NMEA 2000");
  lbl_n2k = lv_label_create(sec);
  lv_obj_set_width(lbl_n2k, LV_PCT(100));
  lv_label_set_long_mode(lbl_n2k, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_n2k, "");
  make_switch_row(sec, "Twin engines: start page shows port and starboard RPM", n2kSettings.twin, twin_cb);
  make_btn(sec, LV_SYMBOL_LIST " Devices, gauge ranges and limits", n2k_open_cb);
  lv_obj_t *n2k_hint = make_grey_label(sec);
  lv_obj_set_width(n2k_hint, LV_PCT(100));
  lv_label_set_long_mode(n2k_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(n2k_hint, "Every device on the bus, grouped by PGN. Tap a value to set its gauge range and warning/alarm limits. The engine is chosen on the Engine tab.");
  lv_timer_create(n2k_timer_cb, 1000, NULL);

  /* ---- Victron (own file) ---- */
  settings_victron(tab_settings);

  /* ---- Display ---- */
  sec = make_section(tab_settings, LV_SYMBOL_IMAGE "  Display");
  lbl_bl = lv_label_create(sec);
  sl_bl = make_slider(sec, 5, 100, bl_normal, bl_slider_cb);  // min 5% so the screen never goes black
  lbl_bl_saver = lv_label_create(sec);
  sl_bl_saver = make_slider(sec, 0, 100, bl_saver, bl_slider_cb);
  update_bl_labels();

  /* ---- Sensor scan interval ---- */
  sec = make_section(tab_settings, LV_SYMBOL_REFRESH "  Sensor scan interval");
  lbl_scan = lv_label_create(sec);
  make_slider(sec, 1, 10, scan_interval_s, scan_slider_cb);
  update_scan_label();

  /* ---- SD card ---- */
  sec = make_section(tab_settings, LV_SYMBOL_SD_CARD "  SD card");
  make_switch_row(sec, "Write the log to the card", feat_sdlog, sdlog_cb);

  lbl_sdlog = lv_label_create(sec);
  lv_obj_set_width(lbl_sdlog, LV_PCT(100));
  lv_label_set_long_mode(lbl_sdlog, LV_LABEL_LONG_WRAP);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SD_CARD " Mount", sd_mount_cb);
  make_btn(row, LV_SYMBOL_EJECT " Eject", sd_eject_cb);
  make_btn(row, LV_SYMBOL_REFRESH, sd_refresh_cb);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_FILE " New log", sd_newfile_cb);
  lv_obj_t *del_logs = make_btn(row, LV_SYMBOL_TRASH " Delete old logs", sd_delete_cb);
  lv_obj_set_style_bg_color(del_logs, lv_palette_main(LV_PALETTE_RED), 0);

  /* measurements as CSV */
  make_switch_row(sec, "Log measurements to /data.csv", feat_csv, csv_cb);
  row = make_row(sec, LV_FLEX_ALIGN_START);
  lv_label_set_text(lv_label_create(row), "Every");
  dd_csv = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd_csv, "1 min\n5 min\n15 min\n60 min");
  lv_dropdown_set_selected(dd_csv, csv_interval_min == 1 ? 0 : csv_interval_min == 5 ? 1
                                                            : csv_interval_min == 15  ? 2
                                                                                      : 3);
  lv_obj_add_event_cb(dd_csv, csv_interval_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_csv = lv_label_create(sec);
  lv_obj_set_width(lbl_csv, LV_PCT(100));
  lv_label_set_long_mode(lbl_csv, LV_LABEL_LONG_WRAP);
  update_csv_label();

  /* settings backup */
  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Back up settings", backup_cb);
  make_btn(row, LV_SYMBOL_UPLOAD " Restore", restore_cb);

  lv_obj_t *sd_hint = make_grey_label(sec);
  lv_obj_set_width(sd_hint, LV_PCT(100));
  lv_label_set_long_mode(sd_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(sd_hint, "Eject before pulling the card out. Files can be downloaded from the web page under /files. The backup contains WiFi and Victron keys, so keep the card safe. Restoring restarts the board.");
  update_sdlog_label();
}
