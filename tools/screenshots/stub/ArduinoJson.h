#pragma once
/* Just enough of ArduinoJson 7 for n2k_settings.cpp to compile (the backup is not used here) */
#include <Arduino.h>
struct JsonVariant {
  template <class T> JsonVariant &operator=(const T &) { return *this; }
  template <class T> T operator|(T d) const { return d; }
  const char *operator|(const char *d) const { return d; }
  template <class T> T to() { return T(); }
  JsonVariant operator[](const char *) const { return JsonVariant(); }
  bool isNull() const { return true; }
};
struct JsonObject : JsonVariant {
  JsonObject() {}
  JsonObject(const JsonVariant &) {}
};
struct JsonDocument : JsonVariant {};
