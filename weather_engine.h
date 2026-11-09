#pragma once
#include <Arduino.h>
#include <time.h>

struct WeatherSummary {
  bool valid = false;
  int samples = 0;
  int symbol = 0;
  float minTemp = NAN, maxTemp = NAN, centerTemp = NAN;
  float maxWind = NAN, maxRisk = NAN, maxRain = NAN;
};

// Sex representativa prognostidpunkter inom kommande 24 timmar.
struct WeatherHourSample {
  bool valid = false;
  bool rainValid = false;
  time_t at = 0;
  float temperature = NAN;
  float wind = NAN;
  float rainMm = NAN; // summerad prognosmangd under 4-timmarsintervallet
  int symbol = 0;
};

struct WeatherSnapshot {
  bool valid = false;
  time_t fetchedAt = 0;
  char createdUtc[32] = {};
  char periodName[40] = {};
  time_t periodStart = 0, periodEnd = 0;
  WeatherSummary main;
  WeatherSummary days[5];
  WeatherHourSample hours[6];
  time_t dayStarts[5] = {};
};

// Anropas en gång efter Serial.begin och ui_init(). Returnerar direkt.
void weather_begin();
// Hämtar en säker kopia av senast beräknade prognosen; false tills en prognos finns.
bool weather_get(WeatherSnapshot &out);
// Svenska standard-/sommartid. Säker att kalla från LVGL-tråden.
void weather_local_clock(char *out, size_t len);
const char *weather_name(int symbol);
