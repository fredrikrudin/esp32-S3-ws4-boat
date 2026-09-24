// n2k_bus.cpp - receives NMEA 2000 PGNs and stores them in the generic data store
//
// Libraries (Arduino IDE):
//   - NMEA2000 by Timo Lappalainen         https://github.com/ttlappalainen/NMEA2000
//   - NMEA2000_esp32_twai by Sergei P.     https://github.com/sergei/NMEA2000_esp32_twai
//     (ESP32-S3 needs the TWAI based driver; the old NMEA2000_esp32 does not build for S3)

#include "n2k_config.h"
#define ESP32_CAN_TX_PIN N2K_CAN_TX_PIN
#define ESP32_CAN_RX_PIN N2K_CAN_RX_PIN
#include <NMEA2000_esp32_twai.h>
#include <N2kMessages.h>
#include <N2kDeviceList.h>

#include "n2k_bus.h"
#include "n2k_data.h"
#include "n2k_settings.h"
#include "n2k_limits.h"

static NMEA2000_esp32_twai *s_n2k = nullptr;
static tN2kDeviceList *s_devList = nullptr;   // names from address claim / product info
static volatile uint32_t s_msgs = 0, s_lastMsgMs = 0;

// PGNs we handle
static const unsigned long kRxPgns[] = {
  127488L,  // Engine parameters, rapid (RPM)
  127489L,  // Engine parameters, dynamic (oil, coolant, alternator, hours, status)
  127505L,  // Fluid level (tanks)
  127508L,  // Battery status (volts)
  128259L,  // Speed through water
  128267L,  // Water depth
  129025L,  // Position, rapid
  129026L,  // COG & SOG, rapid
  130310L,  // Environmental (sea temperature)
  130311L,  // Environmental
  130312L,  // Temperature
  130316L,  // Temperature, extended range
  0
};
static const unsigned long kFastPackets[] = { 127489L, 0 };

static inline bool has(double v) { return !N2kIsNA(v); }
static inline double k2c(double k) { return k - 273.15; }
static inline double ms2kn(double v) { return v * 1.9438445; }
static inline double rad2deg(double r) { double d = r * 57.2957795; return d < 0 ? d + 360.0 : d; }

// Engine messages: optional source-address filter (for two engine gateways
// that both report instance 0, set engSource to the one you want).
static inline bool engineSrcOk(uint8_t src) {
  return n2kSettings.engSource == 255 || n2kSettings.engSource == src;
}

static void storeTemp(tN2kTempSource ts, uint8_t inst, double k, uint8_t src) {
  if (!has(k)) return;
  if (ts == N2kts_SeaTemperature) n2kSet(Q_SEA_TEMP, inst, 0, k2c(k), src);
  else                            n2kSet(Q_TEMP, inst, (uint8_t)ts, k2c(k), src);
}

static void onMsg(const tN2kMsg &m) {
  s_msgs++;
  s_lastMsgMs = millis();
  n2kNoteSeen(m.PGN, m.Source);
  n2kSetPgnContext(m.PGN);
#if N2K_DEBUG_SERIAL >= 2
  Serial.printf("[N2K] PGN %lu src %u len %u\n", (unsigned long)m.PGN, m.Source, m.DataLen);
#endif
  const uint8_t src = m.Source;

  switch (m.PGN) {
    case 127488L: {
      unsigned char inst; double rpm, boost; int8_t trim;
      if (engineSrcOk(src) && ParseN2kEngineParamRapid(m, inst, rpm, boost, trim) && has(rpm))
        n2kSet(Q_ENG_RPM, inst, 0, rpm, src);
    } break;

    case 127489L: {
      unsigned char inst;
      double oilP, oilT, coolT, altV, fuelRate, hours, coolP, fuelP;
      int8_t load, torque;
      tN2kEngineDiscreteStatus1 s1;
      tN2kEngineDiscreteStatus2 s2;
      if (engineSrcOk(src) &&
          ParseN2kEngineDynamicParam(m, inst, oilP, oilT, coolT, altV, fuelRate, hours,
                                     coolP, fuelP, load, torque, s1, s2)) {
        if (has(oilP))     n2kSet(Q_ENG_OIL_P,     inst, 0, oilP / 100000.0, src);  // Pa -> bar
        if (has(oilT))     n2kSet(Q_ENG_OIL_T,     inst, 0, k2c(oilT), src);
        if (has(coolT))    n2kSet(Q_ENG_COOL_T,    inst, 0, k2c(coolT), src);
        if (has(altV))     n2kSet(Q_ENG_ALT_V,     inst, 0, altV, src);
        if (has(fuelRate)) n2kSet(Q_ENG_FUEL_RATE, inst, 0, fuelRate, src);          // L/h
        if (has(hours))    n2kSet(Q_ENG_HOURS,     inst, 0, hours / 3600.0, src);    // s -> h
        n2kSet(Q_ENG_STATUS1, inst, 0, s1.Status, src);
        n2kSet(Q_ENG_STATUS2, inst, 0, s2.Status, src);
      }
    } break;

    case 127505L: {
      unsigned char inst; tN2kFluidType ft; double level, cap;
      if (ParseN2kFluidLevel(m, inst, ft, level, cap)) {
        n2kTankFindOrAdd((uint8_t)ft, inst);
        if (has(level)) n2kSet(Q_TANK_LEVEL, inst, (uint8_t)ft, level, src);   // %
        if (has(cap))   n2kSet(Q_TANK_CAP,   inst, (uint8_t)ft, cap, src);     // L
      }
    } break;

    case 127508L: {
      unsigned char inst, sid; double v, a, t;
      if (ParseN2kDCBatStatus(m, inst, v, a, t, sid)) {
        if (has(v)) n2kSet(Q_BATT_V, inst, 0, v, src);
        if (has(a)) n2kSet(Q_BATT_A, inst, 0, a, src);
      }
    } break;

    case 128259L: {
      unsigned char sid; double stw, sog; tN2kSpeedWaterReferenceType ref;
      if (ParseN2kBoatSpeed(m, sid, stw, sog, ref) && has(stw))
        n2kSet(Q_STW, 0, 0, ms2kn(stw), src);
    } break;

    case 128267L: {
      unsigned char sid; double depth, offset;
      if (ParseN2kWaterDepth(m, sid, depth, offset)) {
        if (has(depth))  n2kSet(Q_DEPTH, 0, 0, depth, src);
        if (has(offset)) n2kSet(Q_DEPTH_OFFSET, 0, 0, offset, src);
      }
    } break;

    case 129025L: {
      double lat, lon;
      if (ParseN2kPositionRapid(m, lat, lon) && has(lat) && has(lon)) {
        n2kSet(Q_LAT, 0, 0, lat, src);
        n2kSet(Q_LON, 0, 0, lon, src);
      }
    } break;

    case 129026L: {
      unsigned char sid; tN2kHeadingReference ref; double cog, sog;
      if (ParseN2kCOGSOGRapid(m, sid, ref, cog, sog)) {
        if (has(cog)) n2kSet(Q_COG, 0, 0, rad2deg(cog), src);
        if (has(sog)) n2kSet(Q_SOG, 0, 0, ms2kn(sog), src);
      }
    } break;

    case 130310L: {
      unsigned char sid; double water, air, press;
      if (ParseN2kOutsideEnvironmentalParameters(m, sid, water, air, press) && has(water))
        n2kSet(Q_SEA_TEMP, 0, 0, k2c(water), src);
    } break;

    case 130311L: {
      unsigned char sid; tN2kTempSource ts; double t; tN2kHumiditySource hs; double h, p;
      if (ParseN2kEnvironmentalParameters(m, sid, ts, t, hs, h, p)) storeTemp(ts, 0, t, src);
    } break;

    case 130312L: {
      unsigned char sid, inst; tN2kTempSource ts; double t, set;
      if (ParseN2kTemperature(m, sid, inst, ts, t, set)) storeTemp(ts, inst, t, src);
    } break;

    case 130316L: {
      unsigned char sid, inst; tN2kTempSource ts; double t, set;
      if (ParseN2kTemperatureExt(m, sid, inst, ts, t, set)) storeTemp(ts, inst, t, src);
    } break;

    default: break;   // still listed in the PGN sniffer (n2kJson)
  }
}

// Copy device names into our own thread-safe table (every 2 s, when changed).
// In listen-only mode names only arrive when devices broadcast them (usually at
// power-up); as a node (N2K_LISTEN_ONLY false) the list requests missing info.
static void syncDevices() {
  static uint32_t last = 0;
  if (!s_devList || millis() - last < 2000) return;
  last = millis();
  if (!s_devList->ReadResetIsListUpdated()) return;
  for (uint8_t src = 0; src < 254; src++) {
    const tNMEA2000::tDevice *d = s_devList->FindDeviceBySource(src);
    if (d) n2kDeviceUpdate(src, d->GetManufacturerCode(), d->GetDeviceFunction(),
                           d->GetDeviceClass(), d->GetModelID());
  }
}

#if N2K_OWN_TASK
static void n2kTask(void *) {
  for (;;) {
    s_n2k->ParseMessages();
    syncDevices();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
#endif

void n2kBegin() {
  n2kSettingsLoad();
  n2kLimitsInit();
  n2kDataInit();

  s_n2k = new NMEA2000_esp32_twai(N2K_CAN_TX_PIN, N2K_CAN_RX_PIN, TWAI_MODE_NORMAL, 10, N2K_RX_QUEUE_LEN);
  s_n2k->SetN2kCANMsgBufSize(8);
  s_n2k->SetN2kCANReceiveFrameBufSize(150);

  uint32_t unique = (uint32_t)(ESP.getEfuseMac() & 0x1FFFFF);   // 21 bits, differs per board
  s_n2k->SetProductInformation("WS4BOAT", 100, "ESP32-S3 WS4 Boat Display", "1.0.0", "1.0.0");
  s_n2k->SetDeviceInformation(unique, 130 /*display*/, 120 /*display class*/, 2046 /*unregistered*/);

  s_n2k->SetMode(N2K_LISTEN_ONLY ? tNMEA2000::N2km_ListenOnly : tNMEA2000::N2km_ListenAndNode, 40);
  s_n2k->EnableForward(false);
  s_n2k->ExtendReceiveMessages(kRxPgns);
  s_n2k->ExtendFastPacketMessages(kFastPackets);
  s_n2k->SetMsgHandler(onMsg);
  s_devList = new tN2kDeviceList(s_n2k);

  bool ok = s_n2k->Open();
  Serial.printf("[N2K] CAN TX=%d RX=%d, %s, open %s\n", (int)N2K_CAN_TX_PIN, (int)N2K_CAN_RX_PIN,
                N2K_LISTEN_ONLY ? "listen-only" : "node", ok ? "OK" : "FAILED");
  Serial.printf("[N2K] Engine tab: \"%s\" instance %u, source %s\n", n2kSettings.engName,
                n2kSettings.engInstance, n2kSettings.engSource == 255 ? "any" : String(n2kSettings.engSource).c_str());

#if N2K_OWN_TASK
  xTaskCreatePinnedToCore(n2kTask, "n2k", 6144, nullptr, 3, nullptr, N2K_TASK_CORE);
#endif
}

void n2kLoop() {
#if !N2K_OWN_TASK
  if (s_n2k) { s_n2k->ParseMessages(); syncDevices(); }
#endif
}

bool n2kBusOk() { return s_msgs > 0 && (millis() - s_lastMsgMs) < 5000; }
uint32_t n2kMsgCount() { return s_msgs; }
