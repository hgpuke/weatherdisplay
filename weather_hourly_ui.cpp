// Weatherdisplay - sida 2: dygnsprognos i 320x240 px, LVGL 8.3.
// Inga stora canvasbuffertar: temperatur ritas med en lv_line och sex punkter.
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "weather_engine.h"
#include "weather_hourly_ui.h"
#include "wx_settings.h"
#include "src/ui/ui.h"

namespace {
constexpr int SLOTS = 6;
constexpr int LEFT = 10;
constexpr int STEP = 50;
constexpr int RAIN_BASE_Y = 222;
static bool installed = false;
static uint32_t lastPoll = 0;
static time_t lastFetch = 0;
static time_t lastFirstSlot = 0;
static lv_obj_t *clockText = nullptr;
static lv_obj_t *windText = nullptr;
static lv_obj_t *statusText = nullptr;
static lv_obj_t *hourText[SLOTS] = {};
static lv_obj_t *hourIcons[SLOTS] = {};
static lv_obj_t *tempText[SLOTS] = {};
static lv_obj_t *rainBars[SLOTS] = {};
static lv_obj_t *rainText[SLOTS] = {};
static lv_obj_t *dots[SLOTS] = {};
static lv_obj_t *tempLine = nullptr;
static lv_obj_t *shadowLine = nullptr;
static lv_point_t linePoints[SLOTS] = {};

lv_obj_t *pane(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg,
               uint8_t radius = 0) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_set_pos(p, x, y);
  lv_obj_set_size(p, w, h);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_radius(p, radius, 0);
  lv_obj_set_style_pad_all(p, 0, 0);
  lv_obj_set_style_shadow_width(p, 0, 0);
  return p;
}

lv_obj_t *txt(lv_obj_t *parent, const char *value, int x, int y, int w,
              const lv_font_t *font, uint32_t color,
              lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *o = lv_label_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_width(o, w);
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  lv_label_set_text(o, value);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(o, align, 0);
  return o;
}

void textIfNew(lv_obj_t *o, const char *value) {
  if (o && strcmp(lv_label_get_text(o), value)) lv_label_set_text(o, value);
}

// Samma soluppgangsapproximation och koordinater som huvudvyn.
bool nightAt(time_t at) {
  if (at <= 0) return false;
  tm utc = {};
  gmtime_r(&at, &utc);
  constexpr double pi = 3.14159265358979323846;
  const double lat = wx_settings().lat * pi / 180.0;
  const double lon = wx_settings().lon;
  const double g = 2.0 * pi / 365.0 * utc.tm_yday;
  const double dec = .006918 - .399912*cos(g) + .070257*sin(g)
                   - .006758*cos(2*g) + .000907*sin(2*g)
                   - .002697*cos(3*g) + .00148*sin(3*g);
  const double eqt = 229.18 * (.000075 + .001868*cos(g)
                    - .032077*sin(g) - .014615*cos(2*g) - .040849*sin(2*g));
  const double cosHa = cos(90.833*pi/180.0)/(cos(lat)*cos(dec))
                     - tan(lat)*tan(dec);
  if (cosHa >= 1.0) return true;
  if (cosHa <= -1.0) return false;
  const double ha = acos(cosHa)*180.0/pi;
  const double sunrise = 720.0 - 4.0*(lon+ha) - eqt;
  const double sunset  = 720.0 - 4.0*(lon-ha) - eqt;
  const double minute = 60.0*utc.tm_hour + utc.tm_min;
  return minute < sunrise || minute >= sunset;
}

// Alla ikoner har 32x32 px. Filnamnen pekar pa de nya, verkliga SPIFFS-ikonerna.
const char *hourIcon(int symbol, time_t at) {
  const bool night = nightAt(at);
  switch (symbol) {
    case 1: return night ? "S:/hour_night_clear.bin" : "S:/hour_clear.bin";
    case 2: case 3: case 4:
      return night ? "S:/hour_night_partial.bin" : "S:/hour_partly.bin";
    case 5: case 6: return "S:/hour_cloudy.bin";
    case 7: return "S:/hour_fog.bin";
    case 8: case 9: case 10: case 18: case 19: case 20:
      return "S:/hour_rain.bin";
    case 11: case 21: return "S:/hour_thunder.bin";
    case 12: case 13: case 14: case 22: case 23: case 24:
      return "S:/hour_sleet.bin";
    case 15: case 16: case 17: case 25: case 26: case 27:
      return "S:/hour_snow.bin";
    default: return nullptr;
  }
}

void create() {
  lv_obj_t *screen = ui_WeatherHourly;
  if (!screen) return;
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x76B9E8), 0);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0xA9D9F5), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // De tva genererade elementen behalls: bakatknappen fungerar redan.
  if (ui_WeatherHourly_label) lv_obj_add_flag(ui_WeatherHourly_label, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *header = pane(screen, 0, 0, 320, 36, 0x074278);
  lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x0F5B99), 0);
  lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
  pane(screen, 4, 5, 5, 27, 0xF22D46, 2);
  txt(screen, "Dygnsprognos", 43, 8, 196,
      &ui_font_my_barlow_condensed_bold_italic_18, 0xFFFFFF);
  clockText = txt(screen, "--:--", 252, 9, 59,
      &ui_font_my_barlow_bold_14, 0xFFFFFF, LV_TEXT_ALIGN_RIGHT);

  pane(screen, 8, 41, 151, 19, 0xD8EDFA, 5);
  txt(screen, "Nästa 24 timmar", 16, 42, 139,
      &ui_font_my_barlow_bold_14, 0x153D66);
  pane(screen, 214, 41, 98, 19, 0xC6E0F5, 5);
  windText = txt(screen, "Vind -- m/s", 215, 42, 96,
      &ui_font_my_barlow_condensed_bold_italic_14, 0x153D66,
      LV_TEXT_ALIGN_CENTER);

  pane(screen, 9, 63, 302, 126, 0x2675B3, 6);
  for (int i = 1; i < SLOTS; i += 2) {
    pane(screen, LEFT + STEP*i, 64, 50, 124, 0x3783BF);
  }
  for (int i = 1; i < SLOTS; ++i) {
    pane(screen, LEFT + STEP*i - 1, 64, 1, 124, 0x81B9DD);
  }
  static const int gridRows[] = {139, 162, 184};
  for (int y : gridRows) {
    pane(screen, 13, y, 294, 1, 0x72A6D0);
  }

  // Nedre rad for 4-timmarsnederbord, mm per intervall.
  pane(screen, 9, 191, 302, 46, 0xD6EAF9, 6);
  txt(screen, "Nederbörd", 16, 192, 114,
      &ui_font_my_barlow_bold_14, 0x123E66);
  txt(screen, "mm / 4 h", 244, 192, 60,
      &ui_font_my_barlow_condensed_regular_14, 0x123E66, LV_TEXT_ALIGN_RIGHT);

  for (int i = 0; i < SLOTS; ++i) {
    const int center = LEFT + 25 + STEP*i;
    hourText[i] = txt(screen, "--", center - 24, 66, 48,
        &ui_font_my_barlow_bold_14, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    hourIcons[i] = lv_img_create(screen);
    lv_obj_set_pos(hourIcons[i], center - 16, 83);
    lv_obj_set_size(hourIcons[i], 32, 32);
    tempText[i] = txt(screen, "--", center-23, 146, 46,
        &ui_font_my_barlow_bold_14, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_bg_color(tempText[i], lv_color_hex(0x12558E), 0);
    lv_obj_set_style_bg_opa(tempText[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tempText[i], 4, 0);

    rainBars[i] = pane(screen, center - 12, RAIN_BASE_Y - 2,
                       24, 2, 0x0781CE, 3);
    rainText[i] = txt(screen, "--", center - 24, 222, 48,
                      &ui_font_my_barlow_bold_14, 0x153D66, LV_TEXT_ALIGN_CENTER);
  }
  // Kurvan ritas med LVGL-objekt, inte med en 320x240-bild eller canvas.
  // Punktarrayen ar statisk eftersom lv_line bara lagrar pekaren.
  shadowLine = lv_line_create(screen);
  lv_obj_set_style_line_color(shadowLine, lv_color_hex(0x0A4D88), 0);
  lv_obj_set_style_line_width(shadowLine, 5, 0);
  lv_obj_set_style_line_rounded(shadowLine, true, 0);
  tempLine = lv_line_create(screen);
  lv_obj_set_style_line_color(tempLine, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_line_width(tempLine, 3, 0);
  lv_obj_set_style_line_rounded(tempLine, true, 0);
  for (int i = 0; i < SLOTS; ++i) {
    const int cx = LEFT + 25 + STEP*i;
    linePoints[i] = {(lv_coord_t)cx, (lv_coord_t)166};
  }
  lv_line_set_points(shadowLine, linePoints, SLOTS);
  lv_line_set_points(tempLine, linePoints, SLOTS);
  lv_obj_add_flag(shadowLine, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tempLine, LV_OBJ_FLAG_HIDDEN);
  for (int i=0; i<SLOTS; ++i) {
    dots[i] = pane(screen, LEFT + 22 + STEP*i, 163, 7, 7, 0xFFFFFF, 7);
    lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
  }
  statusText = txt(screen, "Väntar på SMHI-data", 60, 151, 200,
     &ui_font_my_barlow_bold_14, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);

  // Den gamla LCD Wizard-bakatknappen ar kvar och far sin egen korrekta ikon.
  lv_obj_set_pos(ui_Back_button, 10, 5);
  lv_obj_set_size(ui_Back_button, 29, 27);
  lv_obj_set_style_bg_color(ui_Back_button, lv_color_hex(0x074278), 0);
  lv_obj_set_style_bg_opa(ui_Back_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_Back_button, 0, 0);
  lv_obj_set_style_shadow_width(ui_Back_button, 0, 0);
  lv_obj_set_style_pad_all(ui_Back_button, 0, 0);
  lv_obj_move_foreground(ui_Back_button);
  installed = true;
  Serial.printf("Dygnsprognos: LVGL skapad, heap=%u\n", ESP.getFreeHeap());
}

void redraw(const WeatherSnapshot &w) {
  float lo = NAN, hi = NAN, lowWind = NAN, highWind = NAN, maxRain = 0;
  bool allTemps = true;
  bool anyTemps = false;
  for (int i = 0; i < SLOTS; ++i) {
    const WeatherHourSample &s = w.hours[i];
    if (!s.valid || !isfinite(s.temperature)) { allTemps = false; continue; }
    anyTemps = true;
    if (!isfinite(lo) || s.temperature < lo) lo = s.temperature;
    if (!isfinite(hi) || s.temperature > hi) hi = s.temperature;
    if (isfinite(s.wind)) {
      if (!isfinite(lowWind) || s.wind < lowWind) lowWind = s.wind;
      if (!isfinite(highWind) || s.wind > highWind) highWind = s.wind;
    }
    if (s.rainValid && isfinite(s.rainMm)) maxRain = fmaxf(maxRain, s.rainMm);
  }

  char b[64];
  if (isfinite(lowWind) && isfinite(highWind)) {
    snprintf(b, sizeof(b), "Vind %d-%d m/s", (int)roundf(lowWind), (int)roundf(highWind));
    textIfNew(windText, b);
  } else textIfNew(windText, "Vind -- m/s");
  if (isfinite(lo) && isfinite(hi) && hi-lo < 2.0f) {
    const float m = (hi+lo)/2;
    lo = m-1.0f; hi = m+1.0f;
  }
  if (isfinite(lo) && isfinite(hi)) {
    lo -= 0.2f;
    hi += 0.2f;
  }

  for (int i = 0; i < SLOTS; ++i) {
    const WeatherHourSample &s = w.hours[i];
    tm loc = {};
    localtime_r(&s.at, &loc);
    snprintf(b, sizeof(b), "%02d", loc.tm_hour);
    textIfNew(hourText[i], b);
    if (!s.valid || !isfinite(s.temperature)) {
      textIfNew(tempText[i], "--");
      lv_obj_add_flag(tempText[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(hourIcons[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
      textIfNew(rainText[i], "--");
      lv_obj_add_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const char *path = hourIcon(s.symbol, s.at);
    if (path) {
      const void *cur = lv_img_get_src(hourIcons[i]);
      if (!cur || lv_img_src_get_type(cur) != LV_IMG_SRC_FILE ||
          strcmp((const char *)cur, path)) lv_img_set_src(hourIcons[i], path);
      lv_obj_clear_flag(hourIcons[i], LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(hourIcons[i], LV_OBJ_FLAG_HIDDEN);

    snprintf(b, sizeof(b), "%+d°", (int)roundf(s.temperature));
    textIfNew(tempText[i], b);
    lv_obj_clear_flag(tempText[i], LV_OBJ_FLAG_HIDDEN);
    const float f = (s.temperature - lo) / (hi - lo);
    int y = 178 - (int)roundf(f * 39.f);
    if (y < 139) y=139;
    if (y > 178) y=178;
    linePoints[i].y = (lv_coord_t)y;
    lv_obj_set_pos(tempText[i], LEFT+STEP*i+2, y-24);
    lv_obj_set_pos(dots[i], LEFT+STEP*i+21, y-4);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);

    if (s.rainValid && isfinite(s.rainMm)) {
      if (s.rainMm < 0.05f) snprintf(b, sizeof(b), "0");
      else snprintf(b, sizeof(b), "%.1f", s.rainMm);
      for (char *p = b; *p; ++p) if (*p == '.') *p = ',';
      textIfNew(rainText[i], b);
      if (s.rainMm >= 0.05f) {
        int h = max(2, min(12, (int)roundf(s.rainMm / fmaxf(1.0f, maxRain) * 12)));
        lv_obj_set_pos(rainBars[i], LEFT + STEP*i + 13, RAIN_BASE_Y-h);
        lv_obj_set_size(rainBars[i], 24, h);
        lv_obj_clear_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
      } else lv_obj_add_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      textIfNew(rainText[i], "--");
      lv_obj_add_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (anyTemps) lv_obj_add_flag(statusText, LV_OBJ_FLAG_HIDDEN);
  else {
    textIfNew(statusText, "Tidsprognos saknas");
    lv_obj_clear_flag(statusText, LV_OBJ_FLAG_HIDDEN);
  }
  if (allTemps && isfinite(lo) && isfinite(hi)) {
    lv_obj_invalidate(shadowLine);
    lv_obj_invalidate(tempLine);
    lv_obj_clear_flag(shadowLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(tempLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(shadowLine);
    lv_obj_invalidate(tempLine);
  } else {
    lv_obj_add_flag(shadowLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tempLine, LV_OBJ_FLAG_HIDDEN);
  }
  Serial.printf("Dygnsprognos: 6 tider fran %lld, data uppdaterade %lld, heap=%u\n",
    (long long)w.hours[0].at, (long long)w.fetchedAt, ESP.getFreeHeap());
}
} // namespace

void weather_hourly_ui_install() { if (!installed) create(); }

void weather_hourly_ui_poll() {
  if (!installed || lv_scr_act() != ui_WeatherHourly) return;
  const uint32_t nowMs = millis();
  if (lastPoll && nowMs-lastPoll < 30000) return;
  lastPoll = nowMs;
  char timeText[16];
  weather_local_clock(timeText, sizeof(timeText));
  textIfNew(clockText, timeText);
  WeatherSnapshot snap;
  if (!weather_get(snap)) return;
  if (lastFetch != snap.fetchedAt || lastFirstSlot != snap.hours[0].at) {
    lastFetch = snap.fetchedAt;
    lastFirstSlot = snap.hours[0].at;
    redraw(snap);
  }
}
