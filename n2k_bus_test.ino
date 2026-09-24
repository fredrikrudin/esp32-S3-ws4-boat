// n2k_bus_test.ino - NMEA 2000 bus test for the Waveshare ESP32-S3-Touch-LCD-4 V4
//
// Standalone: no display, no LVGL. Verifies wiring, CAN pins and what the boat sends.
// Output on Serial (115200):
//   - decoded depth, speed, sea temp, battery, position, engine and tank values as they arrive
//   - every 10 s: a table of all PGNs seen, per source address, with message counts
//
// Libraries: NMEA2000 (ttlappalainen) + NMEA2000_esp32_twai (sergei)
// Board: ESP32S3 Dev Module, PSRAM OPI - same settings as the main sketch.
// Prints over the native USB port (HWCDC), like the main firmware.
//
// The display is NOT touched: the backlight stays off. That is expected.

#include <Arduino.h>
#include "HWCDC.h"
HWCDC USBSerial;
#define ESP32_CAN_TX_PIN GPIO_NUM_6   // V4.0 hardware reference: TWAI TX GPIO6
#define ESP32_CAN_RX_PIN GPIO_NUM_0   // V4.0 hardware reference: TWAI RX GPIO0
#include <NMEA2000_esp32_twai.h>
#include <N2kMessages.h>

static NMEA2000_esp32_twai n2k(ESP32_CAN_TX_PIN, ESP32_CAN_RX_PIN, TWAI_MODE_NORMAL, 10, 64);

struct Seen { uint32_t pgn; uint8_t src; uint32_t n; };
static Seen seen[64];
static int seenN = 0;
static uint32_t total = 0;

static void note(uint32_t pgn, uint8_t src) {
  for (int i = 0; i < seenN; i++) if (seen[i].pgn == pgn && seen[i].src == src) { seen[i].n++; return; }
  if (seenN < 64) seen[seenN++] = { pgn, src, 1 };
}

static bool ok(double v) { return !N2kIsNA(v); }

// Print decoded values at most once per second per PGN to keep the log readable
static bool throttle(uint32_t pgn) {
  static uint32_t lastPgn[16]; static uint32_t lastMs[16];
  for (int i = 0; i < 16; i++) {
    if (lastPgn[i] == pgn) { if (millis() - lastMs[i] < 1000) return false; lastMs[i] = millis(); return true; }
    if (lastPgn[i] == 0) { lastPgn[i] = pgn; lastMs[i] = millis(); return true; }
  }
  return true;
}

static void onMsg(const tN2kMsg &m) {
  total++;
  note(m.PGN, m.Source);
  if (!throttle(m.PGN)) return;
  const uint8_t s = m.Source;

  switch (m.PGN) {
    case 128267L: { unsigned char sid; double d, off;
      if (ParseN2kWaterDepth(m, sid, d, off) && ok(d))
        USBSerial.printf("[%3u] Depth %.2f m (offset %.2f)\n", s, d, ok(off) ? off : 0.0); } break;
    case 128259L: { unsigned char sid; double stw, sog; tN2kSpeedWaterReferenceType r;
      if (ParseN2kBoatSpeed(m, sid, stw, sog, r) && ok(stw))
        USBSerial.printf("[%3u] Speed through water %.2f kn\n", s, stw * 1.9438445); } break;
    case 130310L: { unsigned char sid; double w, a, p;
      if (ParseN2kOutsideEnvironmentalParameters(m, sid, w, a, p) && ok(w))
        USBSerial.printf("[%3u] Sea temp %.1f C (130310)\n", s, w - 273.15); } break;
    case 130312L: case 130316L: { unsigned char sid, inst; tN2kTempSource ts; double t, set;
      bool r = m.PGN == 130312L ? ParseN2kTemperature(m, sid, inst, ts, t, set)
                                : ParseN2kTemperatureExt(m, sid, inst, ts, t, set);
      if (r && ok(t)) USBSerial.printf("[%3u] Temp src %d #%u %.1f C (%lu)\n", s, (int)ts, inst, t - 273.15,
                                    (unsigned long)m.PGN); } break;
    case 127508L: { unsigned char inst, sid; double v, a, t;
      if (ParseN2kDCBatStatus(m, inst, v, a, t, sid) && ok(v))
        USBSerial.printf("[%3u] Battery #%u %.2f V\n", s, inst, v); } break;
    case 129025L: { double lat, lon;
      if (ParseN2kPositionRapid(m, lat, lon) && ok(lat))
        USBSerial.printf("[%3u] Position %.5f, %.5f\n", s, lat, lon); } break;
    case 129026L: { unsigned char sid; tN2kHeadingReference ref; double cog, sog;
      if (ParseN2kCOGSOGRapid(m, sid, ref, cog, sog) && ok(sog))
        USBSerial.printf("[%3u] SOG %.2f kn COG %.0f\n", s, sog * 1.9438445, ok(cog) ? cog * 57.29578 : 0.0); } break;
    case 127488L: { unsigned char inst; double rpm, boost; int8_t trim;
      if (ParseN2kEngineParamRapid(m, inst, rpm, boost, trim) && ok(rpm))
        USBSerial.printf("[%3u] Engine #%u %.0f rpm\n", s, inst, rpm); } break;
    case 127489L: { unsigned char inst; double oP, oT, cT, aV, fr, h, cP, fP; int8_t ld, tq;
      tN2kEngineDiscreteStatus1 s1; tN2kEngineDiscreteStatus2 s2;
      if (ParseN2kEngineDynamicParam(m, inst, oP, oT, cT, aV, fr, h, cP, fP, ld, tq, s1, s2))
        USBSerial.printf("[%3u] Engine #%u coolant %.1f C, oil %.2f bar, alt %.2f V, status 0x%04X\n", s, inst,
                      ok(cT) ? cT - 273.15 : NAN, ok(oP) ? oP / 100000.0 : NAN, ok(aV) ? aV : NAN,
                      s1.Status); } break;
    case 127505L: { unsigned char inst; tN2kFluidType ft; double lvl, cap;
      if (ParseN2kFluidLevel(m, inst, ft, lvl, cap) && ok(lvl))
        USBSerial.printf("[%3u] Tank type %d #%u %.1f %% (cap %.0f L)\n", s, (int)ft, inst, lvl, ok(cap) ? cap : 0.0); } break;
  }
}

void setup() {
  USBSerial.begin(115200);
  delay(1500);
  USBSerial.println("\nN2K bus test - Waveshare ESP32-S3-Touch-LCD-4 V4 (CAN TX=6 RX=0)");

  n2k.SetN2kCANMsgBufSize(8);
  n2k.SetN2kCANReceiveFrameBufSize(150);
  n2k.SetMode(tNMEA2000::N2km_ListenOnly);    // never transmits PGNs
  n2k.EnableForward(false);
  n2k.SetMsgHandler(onMsg);
  USBSerial.println(n2k.Open() ? "CAN open OK - waiting for messages..." : "CAN open FAILED");
}

void loop() {
  n2k.ParseMessages();

  static uint32_t last = 0;
  if (millis() - last >= 10000) {
    last = millis();
    USBSerial.printf("\n--- %lu messages total, %d PGN/source pairs ---\n", (unsigned long)total, seenN);
    if (!total) USBSerial.println("No traffic: check CAN-H/CAN-L, ground, N2K power, and that the bus is terminated.");
    for (int i = 0; i < seenN; i++)
      USBSerial.printf("  src %3u  PGN %6lu  %6lu msg\n", seen[i].src, (unsigned long)seen[i].pgn, (unsigned long)seen[i].n);
    USBSerial.println();
  }
}
