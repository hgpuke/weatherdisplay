// SMHI-visning för LCD Wizard LVGL 8.3, 2026-11-05.
#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "weather_engine.h"
#include "weather_ui.h"
#include "wx_settings.h"
#include "src/ui/ui.h"

namespace {
uint32_t lastDraw = 0;
void label(lv_obj_t *obj, const char *value) {
  if (!obj || !value) return;
  const char *old = lv_label_get_text(obj);
  if (!old || strcmp(old, value)) lv_label_set_text(obj, value);
}
int temp(float x) { return (int)lroundf(x); }

// Automatisk symbolval: ENDAST var godkanda uppsattning.
// Stora symboler ar 96x96; sma symboler ar 42x42 (RGB565A8 fran SPIFFS).
// Huvudikonen valjs efter soluppgang/solnedgang for prognosens mittpunkt.
// Femdygnsikonerna ar avsiktligt dag/neutrala.
// Solens upp/nedgang vid ortens sparade koordinater, samma som SMHI.
// NOAA:s approximation: solcentrum 90.833 grader fran zenit vid horisonten.
bool forecastIsNight(const WeatherSnapshot &w) {
  if (w.periodStart <= 0 || w.periodEnd <= w.periodStart) return false;
  const time_t at = w.periodStart + (w.periodEnd - w.periodStart) / 2;
  struct tm utc = {};
  gmtime_r(&at, &utc);
  const double pi = 3.14159265358979323846;
  const double lat = wx_settings().lat * pi / 180.0;
  const double lon = wx_settings().lon;
  const double gamma = 2.0*pi/365.0 * (utc.tm_yday);
  const double decl = 0.006918 - 0.399912*cos(gamma) + 0.070257*sin(gamma)
                    - 0.006758*cos(2*gamma) + 0.000907*sin(2*gamma)
                    - 0.002697*cos(3*gamma) + 0.00148*sin(3*gamma);
  const double eqtime = 229.18 * (0.000075 + 0.001868*cos(gamma)
                        - 0.032077*sin(gamma) - 0.014615*cos(2*gamma)
                        - 0.040849*sin(2*gamma));
  const double cosHA = cos(90.833*pi/180.0)/(cos(lat)*cos(decl))
                       - tan(lat)*tan(decl);
  if (cosHA >= 1.0) return true;  // Ingen soluppgang (polarnatt).
  if (cosHA <= -1.0) return false; // Ingen solnedgang (midnattssol).
  const double haDeg = acos(cosHA)*180.0/pi;
  const double sunriseUtcMin = 720.0 - 4.0*(lon+haDeg) - eqtime;
  const double sunsetUtcMin  = 720.0 - 4.0*(lon-haDeg) - eqtime;
  const double timeUtcMin = 60.0*utc.tm_hour + utc.tm_min;
  return timeUtcMin < sunriseUtcMin || timeUtcMin >= sunsetUtcMin;
}

const char *iconFor(int code, bool large, bool night) {
  if (large) {
    switch (code) {
      case 1: return night ? "S:/big_night_clear.bin" : "S:/big_day_clear.bin";
      case 2: case 3: case 4: return night ? "S:/big_night_partial_cloud.bin" : "S:/partly_cloudy_96x96_tr.bin";
      case 5: case 6: return "S:/big_cloudy.bin";
      case 7: return "S:/big_fog.bin";
      case 8: case 9: case 10: case 18: case 19: case 20:
        return "S:/big_day_rain.bin";
      case 11: case 21: return "S:/big_thunder.bin";
      case 12: case 13: case 14: case 22: case 23: case 24:
        return "S:/big_sleet.bin";
      case 15: case 16: case 17: case 25: case 26: case 27:
        return "S:/big_snow.bin";
      default: return "S:/big_cloudy.bin";
    }
  }
  switch (code) {
    case 1: return "S:/clear.bin";
    case 2: case 3: case 4: return "S:/partly_cloudy.bin";
    case 5: case 6: return "S:/wx_cloudy.bin";
    case 7: return "S:/fog.bin";
    case 8: case 9: case 10: case 18: case 19: case 20:
      return "S:/rain_2.bin";
    case 11: case 21: return "S:/wx_thunder.bin";
    case 12: case 13: case 14: case 22: case 23: case 24:
      return "S:/wx_sleet.bin";
    case 15: case 16: case 17: case 25: case 26: case 27:
      return "S:/snow_2.bin";
    default: return "S:/wx_cloudy.bin";
  }
}

void icon(lv_obj_t *obj, int code, bool large, bool night = false) {
  if (!obj || code <= 0) return;
  const char *path = iconFor(code, large, night);
  const void *current = lv_img_get_src(obj);
  if (!current || lv_img_src_get_type(current) != LV_IMG_SRC_FILE ||
      strcmp(static_cast<const char *>(current), path) != 0) {
    lv_img_set_src(obj, path);
    Serial.printf("SMHI-symbol %d (%s): %s\n", code, large ? "stor" : "liten", path);
  }
  if (lv_img_get_zoom(obj) != 256) lv_img_set_zoom(obj, 256);
  if (large) {
    // Bevara den redan godkanda placeringen fran LCD Wizard.
    lv_obj_set_size(obj, 96, 96);
  } else {
    // Varje prognospanel ar 62x80; rubriken ar 18 px och temperaturen
    // borjar vid y=60. Mittpositionen for 42x42 blir (10, 18).
    // Fixar den varierande x/y-placeringen i LCD Wizards fem forlagor.
    lv_obj_set_size(obj, 42, 42);
    lv_obj_set_pos(obj, 10, 18);
  }
}

// Ett engangstest av all utrustning: felaktiga bilder gav tidigare gula moln.
void checkIconsOnSPIFFS() {
  struct Asset { const char *name; size_t bytes; };
  static const Asset files[] = {
    {"/partly_cloudy_96x96_tr.bin", 27652},
    {"/big_night_clear.bin", 27652}, {"/big_night_partial_cloud.bin", 27652},
    {"/big_day_clear.bin", 27652}, {"/big_cloudy.bin", 27652},
    {"/big_day_rain.bin", 27652}, {"/big_snow.bin", 27652},
    {"/big_sleet.bin", 27652}, {"/big_fog.bin", 27652},
    {"/big_thunder.bin", 27652},
    {"/clear.bin", 5296}, {"/partly_cloudy.bin", 5296},
    {"/wx_cloudy.bin", 5296}, {"/fog.bin", 5296},
    {"/rain_2.bin", 5296}, {"/snow_2.bin", 5296},
    {"/wx_sleet.bin", 5296}, {"/wx_thunder.bin", 5296}
  };
  unsigned missing = 0;
  for (const Asset &asset : files) {
    File f = SPIFFS.open(asset.name, FILE_READ);
    if (!f || f.size() != asset.bytes) {
      Serial.printf("IKONFEL %s (forvantat %u byte, funnet %u)\n",
                    asset.name, unsigned(asset.bytes), unsigned(f ? f.size() : 0));
      missing++;
    }
    if (f) f.close();
  }
  Serial.printf("Ikonfiler: %u/18 OK\n", 18-missing);
}
const char *dayName(time_t at) {
  static const char* days[] = {"Sön","Mån","Tis","Ons","Tor","Fre","Lör"};
  struct tm tmv={}; localtime_r(&at,&tmv);
  return days[tmv.tm_wday];
}
void draw() {
  char text[96];
  weather_local_clock(text,sizeof(text));
  label(ui_Clock_Label,text);
  WeatherSnapshot w;
  if (!weather_get(w)) {
    label(ui_Uppdated_Label,"Väntar på SMHI");
    return;
  }
  label(ui_Localtion_Label,"Vädret i");
  label(ui_Location_Label,wx_settings().city.c_str());
  if (w.fetchedAt > 0) {
    struct tm local={}; localtime_r(&w.fetchedAt,&local);
    strftime(text,sizeof(text),"Uppdaterad %H:%M",&local);
    label(ui_Uppdated_Label,text);
  }
  if (w.main.valid) {
    if (isfinite(w.main.centerTemp)) {
      snprintf(text,sizeof(text),"%+d°",temp(w.main.centerTemp));
      label(ui_Temperature_Label,text);
    }
    label(ui_Condition_Label, weather_name(w.main.symbol));
    icon(ui_Main_Weather_Icon, w.main.symbol, true, forecastIsNight(w));
    // Keep the label "Känns som" and update only the separate value field.
    float apparent = w.main.centerTemp;
    if (isfinite(apparent) && isfinite(w.main.maxWind) &&
        apparent <= 10.0f && w.main.maxWind > (4.8f / 3.6f)) {
      // Canadian wind-chill formula (km/h); maxWind is a conservative proxy.
      const float v = w.main.maxWind * 3.6f;
      const float vp = powf(v, 0.16f);
      apparent = 13.12f + 0.6215f * apparent - 11.37f * vp + 0.3965f * apparent * vp;
    }
    if (isfinite(apparent)) {
      snprintf(text,sizeof(text),"%+d°",temp(apparent));
      label(ui_degree_label,text);
    }
    if (isfinite(w.main.maxWind)) {
      snprintf(text,sizeof(text),"%.1f m/s",w.main.maxWind);
      label(ui_Label7,text);
    }
    if (isfinite(w.main.maxRisk)) {
      snprintf(text,sizeof(text),"%.0f%%",w.main.maxRisk);
      label(ui_Label8,text);
    }
  } else label(ui_Condition_Label,"Prognos saknas");
  lv_obj_t *weekdays[5]={ui_Label2,ui_Label3,ui_Label4,ui_Label5,ui_Label6};
  lv_obj_t *temps[5]={ui_Day1_temperatures,ui_Day1_temperatures1,
                       ui_Day1_temperatures2,ui_Day1_temperatures3,
                       ui_Day1_temperatures4};
  lv_obj_t *images[5]={ui_Day1_image,ui_Day1_image1,ui_Day1_image2,
                        ui_Day1_image3,ui_Day1_image4};
  for (int i=0;i<5;i++) {
    if (w.dayStarts[i]>0) label(weekdays[i],dayName(w.dayStarts[i]));
    const WeatherSummary &day=w.days[i];
    if (!day.valid || !isfinite(day.minTemp) || !isfinite(day.maxTemp)) {
      label(temps[i],"--/--");
      continue;
    }
    snprintf(text,sizeof(text),"%d°/%d°",temp(day.maxTemp),temp(day.minTemp));
    label(temps[i],text);
    icon(images[i],day.symbol,false);
  }
}
} // namespace
void weather_ui_poll(void) {
  static bool assetsChecked = false;
  if (!assetsChecked) {
    assetsChecked = true;
    checkIconsOnSPIFFS();
  }
  const uint32_t now=millis();
  if (lastDraw && now-lastDraw<1000) return;
  lastDraw=now;
  draw();
}
