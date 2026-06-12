#pragma once

#include <Arduino.h>

struct SensorSnapshot {
  int soilRaw = 0;
  int soilPercent = 0;
  int waterRaw = 0;
  int waterPercent = 0;
  int rainRaw = 0;
  int rainPercent = 0;

  float ds18b20TempC = NAN;
  float dhtTempC = NAN;
  float dhtHumidity = NAN;

  bool ds18b20Valid = false;
  bool dhtValid = false;
  uint32_t sampledAtMs = 0;
};
