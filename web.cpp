/* Minimal web server (port 80).
 *   /        HTML page: boat data (NMEA 2000), battery SOC, Victron devices, temperatures, weather.
 *            Refreshes itself every 5 seconds.
 *   /json    The same data as JSON, for scripts or Home Assistant (?key=<password> if a password is set).
 *   /api/n2k      all NMEA 2000 data: devices -> PGNs -> values with limits, tanks, engine settings
 *   /api/n2k/set  POST key=...&value=...: engine, tank names, gauge ranges and limits (N2K_INTEGRATION.md)
 *   /login   Password-only login, when a password is set under Settings -> Web page.
 *   /logout
 *   /log     the most recent log lines as plain text (same log as the Serial Monitor)
 *   /files   list of files on the TF card; /files?name=... downloads one
 * Reachable at http://waveshare.local/ (mDNS) or the board's IP address.
 * Starts as soon as WiFi is connected. Runs from loop(), so it never races the UI.
 *
 * Note: plain HTTP, so the password travels unencrypted on the local network.
 * It keeps casual visitors on the same WiFi out; it is not strong security. */
#include "app.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "n2k_bus.h"
#include "n2k_data.h"
#include "n2k_settings.h"
#include "n2k_limits.h"

static WebServer server(80);
static bool started = false;

/* ---------- text helpers (used everywhere below) ---------- */
static void add(String &s, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  s += buf;
}

/* names are typed by the user: escape them for HTML / JSON */
static String esc_html(const char *in) {
  String o;
  for (; *in; in++) {
    if (*in == '<') o += "&lt;";
    else if (*in == '>') o += "&gt;";
    else if (*in == '&') o += "&amp;";
    else if (*in == '"') o += "&quot;";
    else o += *in;
  }
  return o;
}

static String esc_json(const char *in) {
  String o;
  for (; *in; in++) {
    if (*in == '"' || *in == '\\') o += '\\';
    if ((unsigned char)*in >= 0x20) o += *in;
  }
  return o;
}

static void num_json(String &s, const char *key, float v, int decimals) {
  if (isnan(v)) add(s, "\"%s\":null", key);
  else add(s, "\"%s\":%.*f", key, decimals, v);
}

/* ---------- login ---------- */
static char token[33] = "";     // session token in the login cookie; new after boot or password change
static uint32_t last_fail = 0;  // wrong password: next attempt allowed after 2 s

static void new_token() {
  for (int i = 0; i < 16; i++) sprintf(token + i * 2, "%02x", (unsigned)(esp_random() & 0xFF));
}

void web_password_changed() {
  new_token();  // everyone logged in has to log in again
}

static void get_page_name(char *out, size_t n) {
  LOCK();
  strlcpy(out, g.web_name, n);
  UNLOCK();
  if (!out[0]) strlcpy(out, DEFAULT_WEB_NAME, n);
}

static void get_password(char *out) {
  LOCK();
  strlcpy(out, g.web_pass, 33);
  UNLOCK();
}

static bool authorized() {
  char pass[33];
  get_password(pass);
  if (!pass[0]) return true;  // no password set: open page
  if (server.hasHeader("Cookie") && server.header("Cookie").indexOf(String("boat=") + token) >= 0) return true;
  if (server.hasArg("key") && server.arg("key") == pass) return true;  // for scripts: /json?key=...
  return false;
}

static const char PAGE_HEAD[] PROGMEM =
  "<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>"
  "body{font-family:sans-serif;background:#15171a;color:#e0e0e0;margin:0;padding:16px;max-width:520px}"
  "h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;color:#2196f3;margin:20px 0 6px}"
  ".soc{font-size:56px;font-weight:bold}.sub{color:#9e9e9e;font-size:13px}"
  ".row{display:flex;justify-content:space-between;padding:8px 10px;background:#282b30;border-radius:6px;margin:4px 0}"
  ".on{color:#4caf50;font-weight:bold}.off{color:#9e9e9e}.chg{color:#f39c12;font-weight:bold}"
  ".bar{height:10px;background:#1f5f8b;border-radius:5px;overflow:hidden;margin:6px 0}"
  ".bar div{height:100%;background:#3498db}a{color:#2196f3}"
  ".hero{display:flex;gap:14px;align-items:stretch;margin:10px 0 4px}"
  ".g{width:16px;border-radius:8px;background:#10202c;position:relative}"
  ".g i{position:absolute;bottom:0;left:0;width:100%;border-radius:8px}"
  ".gb i{background:linear-gradient(to top,#0b4a7a,#38a9f5)}.gs i{background:linear-gradient(to top,#8a4d06,#f5b32b)}"
  ".gl i{background:linear-gradient(to top,#6e1e16,#e74c3c)}"
  ".mid{flex:1;text-align:center}.clock{font-size:64px;font-weight:bold;line-height:1.1}"
  ".vals{display:flex;justify-content:space-between;font-size:14px;margin-top:6px}"
  ".strip{display:flex;justify-content:space-evenly;background:#1b1f24;border-radius:12px;padding:10px;margin:10px 0}"
  ".fc{display:flex;gap:8px}.fc>div{flex:1;background:#23272e;border-radius:10px;padding:8px;text-align:center;font-size:13px}"
  ".big{font-size:26px}"
  "form.sw{margin:0}.seg{display:flex;border-radius:8px;overflow:hidden}"
  ".seg button,.seg span{font-size:14px;padding:8px 16px;border:1px solid #2f6fb5;background:#0e2233;color:#e0e0e0;width:auto}"
  ".seg button:first-child,.seg span:first-child{border-radius:8px 0 0 8px}"
  ".seg button:last-child,.seg span:last-child{border-radius:0 8px 8px 0}"
  ".seg .act{background:#2f6fb5;color:#fff}"
  "input,button{font-size:16px;padding:10px;border-radius:6px;border:1px solid #3a3f44}"
  "input{background:#282b30;color:#e0e0e0;width:100%;box-sizing:border-box;margin:8px 0}"
  "button{background:#2196f3;color:#fff;border:0;width:100%}.err{color:#f39c12}"
  "</style>";

static void send_login(const char *msg) {
  String s;
  s.reserve(1800);
  char name[24];
  get_page_name(name, sizeof(name));
  s += FPSTR(PAGE_HEAD);
  add(s, "<title>%s</title></head><body><h1>%s</h1>", esc_html(name).c_str(), esc_html(name).c_str());
  s += F("<form method='post' action='/login'>"
         "<input type='password' name='p' placeholder='Password' autofocus>"
         "<button>Log in</button></form>");
  if (msg && msg[0]) {
    s += F("<p class='err'>");
    s += msg;
    s += F("</p>");
  }
  s += F("</body></html>");
  server.send(200, "text/html; charset=utf-8", s);
}

static void redirect(const char *to) {
  server.sendHeader("Location", to);
  server.send(303, "text/plain", "");
}

static void handle_login() {
  if (server.method() != HTTP_POST) {
    send_login("");
    return;
  }
  if (last_fail && millis() - last_fail < 2000) {
    send_login("Too many attempts - wait a moment");
    return;
  }
  char pass[33];
  get_password(pass);
  if (!pass[0] || server.arg("p") == pass) {
    server.sendHeader("Set-Cookie", String("boat=") + token + "; Path=/; HttpOnly; Max-Age=2592000");
    redirect("/");
  } else {
    last_fail = millis() | 1;
    send_login("Wrong password");
  }
}

static void handle_logout() {
  server.sendHeader("Set-Cookie", "boat=; Path=/; Max-Age=0");
  redirect("/login");
}

/* snapshot of everything a page needs */
static VicCfg cfg[MAX_VIC];
static VicData dat[MAX_VIC];
static RuuviTag rtags[MAX_TAGS];
static Weather wx;
static bool wx_has_loc;

static void take_snapshot() {
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);

  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  memcpy(rtags, tags, sizeof(rtags));
  xSemaphoreGive(ruuvi_mtx);

  LOCK();
  wx = g.weather;
  wx_has_loc = g.has_loc;
  UNLOCK();
}

/* temperature of an added RuuviTag, NAN if not heard lately */
static float ruuvi_temp(int slot, uint32_t now) {
  if (!feat_ruuvi || !ruuvi_cfg[slot].used) return NAN;
  for (int i = 0; i < MAX_TAGS; i++)
    if (rtags[i].used && !strcmp(rtags[i].addr, ruuvi_cfg[slot].mac) && rtags[i].has_temp && now - rtags[i].last_seen < 600000UL)
      return rtags[i].temp;
  return NAN;
}

static bool vic_ok(int i, uint32_t now) {
  return cfg[i].used && vic_fresh(dat[i], now) && dat[i].key_ok;
}

/* Is this device putting energy into the battery right now? */
static bool vic_charging(int i, uint32_t now) {
  if (!vic_ok(i, now)) return false;
  const VicData &d = dat[i];
  switch (cfg[i].type) {
    case VIC_SOLAR: return !isnan(d.pv_w) && d.pv_w > 2;
    case VIC_ACCHG: return !isnan(d.batt_i) && d.batt_i > 0.2f;
    case VIC_DCDC: return vic_active_state(d.state);
    default: return false;
  }
}

/* Power in watts for the device list, NAN if not known */
static float vic_power(int i) {
  const VicData &d = dat[i];
  switch (cfg[i].type) {
    case VIC_SOLAR: return d.pv_w;
    case VIC_ACCHG:
    case VIC_BATTMON: return (isnan(d.batt_v) || isnan(d.batt_i)) ? NAN : d.batt_v * d.batt_i;
    case VIC_INVERTER: return d.ac_va;  // VA
    default: return NAN;
  }
}

/* First battery monitor with fresh data, -1 if none */
static int battery_monitor(uint32_t now) {
  for (int i = 0; i < MAX_VIC; i++)
    if (cfg[i].type == VIC_BATTMON && vic_ok(i, now)) return i;
  return -1;
}



/* ---------- NMEA 2000 on the page and in /json ---------- */
static const char *level_class(uint8_t q, uint8_t inst, uint8_t sub, double v, bool checkLow = true) {
  N2kLevel l = n2kLimitsEval(n2kLimitsGet(q, inst, sub), v, checkLow);
  return l == N2K_LVL_ALARM ? " style='color:#f44336'" : l == N2K_LVL_WARN ? " style='color:#ff9800'" : "";
}

static void html_val(String &s, const char *title, bool ok, double v, int dec, const char *unit, const char *cls = "") {
  if (ok) add(s, "<div><span class='sub'>%s</span><br><b%s>%.*f</b> <span class='sub'>%s</span></div>", title, cls, dec, v, unit);
  else add(s, "<div><span class='sub'>%s</span><br>--</div>", title);
}

static void html_boat(String &s) {
  double v, off;
  s += F("<h2>Boat</h2><div class='strip'>");
  bool hd = n2kGet(Q_DEPTH, 0, 0, v);
  if (hd && n2kGet(Q_DEPTH_OFFSET, 0, 0, off, 30000)) v += off;
  html_val(s, "Depth", hd, v, 1, "m", hd ? level_class(Q_DEPTH, 0, 0, v) : "");
  bool h = n2kGet(Q_STW, 0, 0, v);
  html_val(s, "Log", h, v, 1, "kn");
  h = n2kGet(Q_SOG, 0, 0, v);
  html_val(s, "SOG", h, v, 1, "kn");
  h = n2kGetAny(Q_SEA_TEMP, v);
  html_val(s, "Sea", h, v, 1, "&deg;C");
  s += F("</div>");

  const uint8_t e = n2kSettings.engInstance;
  double rpm;
  bool hr = n2kGet(Q_ENG_RPM, e, 0, rpm);
  bool running = hr && rpm > ENG_RUNNING_RPM;
  add(s, "<h2>Engine: %s</h2><div class='strip'>", esc_html(n2kSettings.engName).c_str());
  html_val(s, "RPM", hr, rpm, 0, "", hr ? level_class(Q_ENG_RPM, e, 0, rpm) : "");
  h = n2kGet(Q_ENG_COOL_T, e, 0, v);
  html_val(s, "Coolant", h, v, 0, "&deg;C", h ? level_class(Q_ENG_COOL_T, e, 0, v) : "");
  h = n2kGet(Q_ENG_OIL_P, e, 0, v);
  html_val(s, "Oil", h, v, 1, "bar", h ? level_class(Q_ENG_OIL_P, e, 0, v, running) : "");
  h = n2kGet(Q_ENG_ALT_V, e, 0, v);
  html_val(s, "Alt", h, v, 1, "V", h ? level_class(Q_ENG_ALT_V, e, 0, v, running) : "");
  s += F("</div>");

  if (n2kTankCount()) {
    s += F("<h2>Tanks</h2>");
    for (int i = 0; i < n2kTankCount(); i++) {
      N2kTank *t = n2kTankAt(i);
      double lv, cap;
      bool hl = n2kGet(Q_TANK_LEVEL, t->instance, t->fluidType, lv);
      add(s, "<div class='row'><span>%s</span><span%s>", esc_html(t->name).c_str(),
          hl ? level_class(Q_TANK_LEVEL, t->instance, t->fluidType, lv) : "");
      if (!hl) s += F("--");
      else if (n2kGet(Q_TANK_CAP, t->instance, t->fluidType, cap, 60000) && cap > 0)
        add(s, "<b>%.0f%%</b> <span class='sub'>%.0f/%.0f L</span>", lv, lv * cap / 100, cap);
      else add(s, "<b>%.0f%%</b>", lv);
      s += F("</span></div>");
    }
  }
  s += F("<div class='sub'><a href='/api/n2k'>All NMEA 2000 data (JSON)</a></div>");
}

static void json_num(String &s, const char *key, bool ok, double v, int dec) {
  num_json(s, key, ok ? (float)v : NAN, dec);
}

/* Short boat summary for /json; everything else is at /api/n2k */
static void json_boat(String &s) {
  double v, off;
  s += "\"boat\":{";
  bool hd = n2kGet(Q_DEPTH, 0, 0, v);
  if (hd && n2kGet(Q_DEPTH_OFFSET, 0, 0, off, 30000)) v += off;
  json_num(s, "depth", hd, v, 2);
  s += ',';
  bool h = n2kGet(Q_STW, 0, 0, v);
  json_num(s, "speed_water", h, v, 2);
  s += ',';
  h = n2kGet(Q_SOG, 0, 0, v);
  json_num(s, "sog", h, v, 2);
  s += ',';
  h = n2kGetAny(Q_SEA_TEMP, v);
  json_num(s, "sea_temp", h, v, 1);
  s += ',';
  double lat, lon;
  bool hp = n2kGet(Q_LAT, 0, 0, lat) && n2kGet(Q_LON, 0, 0, lon);
  json_num(s, "lat", hp, lat, 6);
  s += ',';
  json_num(s, "lon", hp, lon, 6);
  const uint8_t e = n2kSettings.engInstance;
  add(s, ",\"engine\":{\"name\":\"%s\",\"instance\":%u,", esc_json(n2kSettings.engName).c_str(), e);
  h = n2kGet(Q_ENG_RPM, e, 0, v);
  json_num(s, "rpm", h, v, 0);
  s += ',';
  h = n2kGet(Q_ENG_COOL_T, e, 0, v);
  json_num(s, "coolant", h, v, 1);
  s += ',';
  h = n2kGet(Q_ENG_OIL_P, e, 0, v);
  json_num(s, "oil_pressure", h, v, 2);
  s += ',';
  h = n2kGet(Q_ENG_ALT_V, e, 0, v);
  json_num(s, "alternator", h, v, 2);
  s += "},\"tanks\":[";
  for (int i = 0; i < n2kTankCount(); i++) {
    N2kTank *t = n2kTankAt(i);
    if (i) s += ',';
    add(s, "{\"name\":\"%s\",\"type\":%u,\"instance\":%u,", esc_json(t->name).c_str(), t->fluidType, t->instance);
    h = n2kGet(Q_TANK_LEVEL, t->instance, t->fluidType, v);
    json_num(s, "level", h, v, 0);
    s += '}';
  }
  s += "]}";
}

/* ---------- HTML page ---------- */
static void handle_root() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  take_snapshot();
  uint32_t now = millis();
  String s;
  s.reserve(8192);  // clock, boat data, tanks, weather, Victron

  char page_name[24];
  get_page_name(page_name, sizeof(page_name));
  s += FPSTR(PAGE_HEAD);
  add(s, "<meta http-equiv='refresh' content='5'><title>%s</title></head><body><h1>%s</h1>",
      esc_html(page_name).c_str(), esc_html(page_name).c_str());

  /* ================= start page: clock with gauges ================= */
  int bm = battery_monitor(now);
  float soc = NAN, batt_v = NAN, batt_w = NAN;
  int ttg = -1;
  if (bm >= 0 && !isnan(dat[bm].soc)) {
    soc = dat[bm].soc;
    batt_v = dat[bm].batt_v;
    if (!isnan(dat[bm].batt_v) && !isnan(dat[bm].batt_i)) batt_w = dat[bm].batt_v * dat[bm].batt_i;
    ttg = dat[bm].remaining_min;
  }

  float pv_sum = 0, yield_sum = 0;
  bool pv_any = false;
  for (int i = 0; i < MAX_VIC; i++) {
    if (cfg[i].type != VIC_SOLAR || !vic_ok(i, now)) continue;
    if (!isnan(dat[i].pv_w)) {
      pv_sum += dat[i].pv_w;
      pv_any = true;
    }
    if (!isnan(dat[i].yield_kwh)) yield_sum += dat[i].yield_kwh;
  }
  static float pv_scale = 400;
  if (pv_sum > pv_scale) pv_scale = pv_sum;

  char clock_txt[16], date_txt[64];
  format_local_time(clock_txt, date_txt);

  s += F("<div class='hero'>");
  add(s, "<div class='g %s'><i style='height:%.0f%%'></i></div>", (!isnan(soc) && soc < 20) ? "gl" : "gb", isnan(soc) ? 0.0f : soc);
  s += F("<div class='mid'>");
  if (!isnan(batt_w) && batt_w > 5) s += F("<div style='color:#2ecc71;font-size:14px'>&#9889; Charging</div>");
  else s += F("<div style='font-size:14px'>&nbsp;</div>");
  add(s, "<div class='clock'>%s</div><div class='sub'>%s</div>", clock_txt, date_txt);
  s += F("</div>");
  add(s, "<div class='g gs'><i style='height:%.0f%%'></i></div>", pv_any ? pv_sum / pv_scale * 100 : 0.0f);
  s += F("</div><div class='vals'><div>");

  if (isnan(soc)) s += F("<b>--</b><br><span class='sub'>No battery data</span>");
  else {
    add(s, "<b class='big'>%.0f%%</b>", soc);
    if (!isnan(batt_v)) {
      if (!isnan(batt_w)) add(s, "<br><span class='sub'>%.2f V &middot; %+.0f W</span>", batt_v, batt_w);
      else add(s, "<br><span class='sub'>%.2f V</span>", batt_v);
    }
    if (ttg >= 0 && !isnan(batt_w) && batt_w < 0) add(s, "<br><span class='sub'>%dh %02dm left</span>", ttg / 60, ttg % 60);
  }
  s += F("</div><div style='text-align:right'>");
  if (!pv_any) s += F("<b>--</b><br><span class='sub'>No solar data</span>");
  else add(s, "<b class='big'>%.0f W</b><br><span class='sub'>Solar &middot; today %.2f kWh</span>", pv_sum, yield_sum);
  s += F("</div></div>");

  /* temperatures in one strip */
  s += F("<div class='strip'>");
  for (int i = 0; i < MAX_RUUVI; i++) {
    float t = ruuvi_temp(i, now);
    if (!ruuvi_cfg[i].used) continue;
    if (isnan(t)) add(s, "<div><span class='sub'>%s</span><br>--</div>", esc_html(ruuvi_cfg[i].name).c_str());
    else add(s, "<div><span class='sub'>%s</span><br><b>%.1f&deg;C</b></div>", esc_html(ruuvi_cfg[i].name).c_str(), t);
  }
  if (wx.valid) add(s, "<div><span class='sub'>Outside</span><br><b>%.1f&deg;C</b></div>", wx.temp);
  s += F("</div>");

  html_boat(s);

  /* ================= weather ================= */
  s += F("<h2>Weather</h2>");
  if (!wx.valid) {
    s += wx_has_loc ? F("<div class='sub'>Loading weather...</div>") : F("<div class='sub'>No location set</div>");
  } else {
    add(s, "<div class='row'><span><b class='big'>%.1f&deg;C</b><br><span class='sub'>%s</span></span>"
           "<span style='text-align:right'>H %.0f&deg; &middot; L %.0f&deg;<br><span class='sub'>Feels %.1f&deg; &middot; %d%% &middot; %.1f m/s</span></span></div>",
        wx.temp, wmo_text(wx.code), wx.today_max, wx.today_min, wx.feels, wx.humidity, wx.wind);
    s += F("<div class='fc'>");
    for (int i = 0; i < 3; i++)
      add(s, "<div><b>%s</b><br><span class='sub'>%s</span><br>%.0f&deg; / %.0f&deg;</div>",
          wx.fc_day[i], wmo_text(wx.fc_code[i]), wx.fc_max[i], wx.fc_min[i]);
    s += F("</div>");
  }

  /* ================= Victron ================= */
  s += F("<h2>Victron devices</h2>");
  bool any = false;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    any = true;
    const char *st = vic_status(dat[i], cfg[i].type, now);
    bool ok = vic_ok(i, now);
    add(s, "<div class='row'><span>%s<br><span class='sub'>%s</span></span><span style='text-align:right'>",
        esc_html(cfg[i].name).c_str(), vic_type_name(cfg[i].type));
    if (!ok) {
      add(s, "<span class='off'>%s</span>", st);
    } else {
      float p = vic_power(i);
      if (!isnan(p)) add(s, "%.0f %s<br>", p, cfg[i].type == VIC_INVERTER ? "VA" : "W");
      if (vic_charging(i, now)) s += F("<span class='chg'>&#9889; Charging</span>");
      else if (cfg[i].type == VIC_INVERTER) {
        const char *alarm = vic_alarm_text(dat[i].alarm);
        add(s, "<span class='sub'>%s</span>", alarm ? alarm : vic_state_name(dat[i].state));
      } else if (cfg[i].type != VIC_BATTMON) {
        add(s, "<span class='sub'>%s</span>", vic_state_name(dat[i].state));
      }
    }
    s += F("</span></div>");
  }
  if (!any) s += F("<div class='sub'>No Victron devices added</div>");

  char pass[33];
  get_password(pass);
  add(s, "<p class='sub'>Updated every 5 s &middot; up %lu min &middot; <a href='/log'>log</a> &middot; <a href='/files'>files</a>%s</p></body></html>", (unsigned long)(now / 60000),
      pass[0] ? " &middot; <a href='/logout'>Log out</a>" : "");
  server.send(200, "text/html; charset=utf-8", s);
}

/* ---------- JSON ---------- */
static void handle_json() {
  if (!authorized()) {
    server.send(401, "application/json", "{\"error\":\"login required\"}");
    return;
  }
  take_snapshot();
  uint32_t now = millis();
  String s;
  s.reserve(3072);

  int bm = battery_monitor(now);
  s += "{\"battery\":{";
  if (bm >= 0) {
    const VicData &d = dat[bm];
    num_json(s, "soc", d.soc, 1);
    s += ',';
    num_json(s, "voltage", d.batt_v, 2);
    s += ',';
    num_json(s, "current", d.batt_i, 2);
    s += ',';
    num_json(s, "power", (isnan(d.batt_v) || isnan(d.batt_i)) ? NAN : d.batt_v * d.batt_i, 0);
    add(s, ",\"source\":\"victron\"");
  } else {
    s += "\"soc\":null";
  }
  s += "},\"victron\":[";

  bool first = true;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    if (!first) s += ',';
    first = false;
    bool ok = vic_ok(i, now);
    add(s, "{\"name\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",", esc_json(cfg[i].name).c_str(),
        vic_type_name(cfg[i].type), vic_status(dat[i], cfg[i].type, now));
    num_json(s, "power", ok ? vic_power(i) : NAN, 0);
    add(s, ",\"charging\":%s", vic_charging(i, now) ? "true" : "false");
    if (ok && cfg[i].type != VIC_BATTMON) add(s, ",\"state\":\"%s\"", vic_state_name(dat[i].state));
    s += '}';
  }
  s += "],\"temperatures\":[";

  bool first_t = true;
  for (int i = 0; i < MAX_RUUVI; i++) {
    if (!ruuvi_cfg[i].used) continue;
    if (!first_t) s += ',';
    first_t = false;
    add(s, "{\"name\":\"%s\",", esc_json(ruuvi_cfg[i].name).c_str());
    num_json(s, "temperature", ruuvi_temp(i, now), 1);
    s += '}';
  }
  s += "],\"weather\":{";
  if (wx.valid) {
    num_json(s, "temperature", wx.temp, 1);
    s += ',';
    num_json(s, "feels_like", wx.feels, 1);
    s += ',';
    add(s, "\"humidity\":%d,", wx.humidity);
    num_json(s, "wind", wx.wind, 1);
    add(s, ",\"condition\":\"%s\",", wmo_text(wx.code));
    num_json(s, "today_max", wx.today_max, 0);
    s += ',';
    num_json(s, "today_min", wx.today_min, 0);
    s += ",\"forecast\":[";
    for (int i = 0; i < 3; i++) {
      if (i) s += ',';
      add(s, "{\"day\":\"%s\",\"condition\":\"%s\",", wx.fc_day[i], wmo_text(wx.fc_code[i]));
      num_json(s, "max", wx.fc_max[i], 0);
      s += ',';
      num_json(s, "min", wx.fc_min[i], 0);
      s += '}';
    }
    s += ']';
  }
  s += "},";
  json_boat(s);
  s += "}";
  server.send(200, "application/json", s);
}

/* The log, as plain text: handy when no computer is attached to the USB port */
static void handle_log() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  String s;
  log_dump(s);
  if (!s.length()) s = "(log is empty)";
  server.send(200, "text/plain; charset=utf-8", s);
}

/* Files on the TF card: a list, or one file as a download */
static void handle_files() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  if (!sd_log_ok()) {
    server.send(200, "text/plain", "No card mounted (Settings -> SD card)");
    return;
  }
  if (server.hasArg("name")) {
    String name = server.arg("name");
    if (name.indexOf("..") >= 0) {  // no climbing out of the root
      server.send(400, "text/plain", "Bad name");
      return;
    }
    if (!name.startsWith("/")) name = "/" + name;
    File f = sd_fs().open(name);
    if (!f || f.isDirectory()) {
      server.send(404, "text/plain", "Not found");
      return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name.substring(1) + "\"");
    server.streamFile(f, "text/plain");
    f.close();
    return;
  }

  String list;
  sd_list_files(list);
  String s;
  s.reserve(1024);
  s += FPSTR(PAGE_HEAD);
  char name[24];
  get_page_name(name, sizeof(name));
  add(s, "<title>%s</title></head><body><h1>Files on the card</h1>", esc_html(name).c_str());
  int start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start);
    if (nl < 0) nl = list.length();
    String line = list.substring(start, nl);
    start = nl + 1;
    int tab = line.indexOf('\t');
    if (tab < 0) continue;
    String fname = line.substring(0, tab);
    long size = line.substring(tab + 1).toInt();
    add(s, "<div class='row'><span><a href='/files?name=%s'>%s</a></span><span class='sub'>%ld kB</span></div>",
        esc_html(fname.c_str()).c_str(), esc_html(fname.c_str()).c_str(), size / 1024);
  }
  s += F("<p class='sub'><a href='/'>back</a></p></body></html>");
  server.send(200, "text/html; charset=utf-8", s);
}

/* ---------- NMEA 2000 API ---------- */
static void handle_n2k() {
  if (!authorized()) {
    server.send(401, "application/json", "{\"error\":\"login required\"}");
    return;
  }
  server.send(200, "application/json", n2kJson());
}

/* POST key=...&value=... (see N2K_INTEGRATION.md). Changes are saved by the network task. */
static void handle_n2k_set() {
  if (!authorized()) {
    server.send(401, "text/plain", "login required");
    return;
  }
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "use POST");
    return;
  }
  bool ok = n2kApplySetting(server.arg("key"), server.arg("value"));
  if (ok) logf("Web: N2K %s = %s", server.arg("key").c_str(), server.arg("value").c_str());
  server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "bad key or value");
}

void web_service() {
  if (!started) {
    if (WiFi.status() != WL_CONNECTED) return;  // start once WiFi is up
    new_token();
    const char *headers[] = { "Cookie" };
    server.collectHeaders(headers, 1);
    server.on("/", handle_root);
    server.on("/json", handle_json);
    server.on("/login", handle_login);
    server.on("/logout", handle_logout);
    server.on("/api/n2k", handle_n2k);
    server.on("/api/n2k/set", handle_n2k_set);
    server.on("/log", handle_log);
    server.on("/files", handle_files);
    server.onNotFound([]() {
      server.send(404, "text/plain", "Not found");
    });
    server.begin();
    started = true;
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
    USBSerial.printf("Web server: http://%s.local/ or http://%s/\n", MDNS_NAME, WiFi.localIP().toString().c_str());
    return;
  }
  server.handleClient();
}
