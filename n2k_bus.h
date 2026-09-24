// n2k_bus.h - NMEA 2000 over the on-board CAN (TWAI) transceiver
#pragma once
#include <Arduino.h>

void     n2kBegin();          // call once in setup(), after Serial
void     n2kLoop();           // call from loop() - does nothing if N2K_OWN_TASK is true
bool     n2kBusOk();          // messages received within the last 5 s
uint32_t n2kMsgCount();       // total N2K messages handled

// Web / SD helpers (n2k_web.cpp) - plug into whatever web server the project uses
String   n2kJson();                                           // full status incl. PGN sniffer
bool     n2kApplySetting(const String &key, const String &value);
String   n2kCsvHeader();
String   n2kCsvLine();
