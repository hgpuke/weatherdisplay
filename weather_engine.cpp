#include "weather_engine.h"
#include "wx_settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

namespace {
// Justera koordinaterna här tills inställningssidan kopplas in.
constexpr int MAX_POINTS = 160;
constexpr uint32_t REFRESH_MS = 30UL * 60UL * 1000UL;
// Endpoint byggs vid varje hamtning fran samma lat/lon som lagrats via BLE.
String forecastUrl() {
  const WxSettings &cfg = wx_settings();
  String url = "https://opendata-download-metfcst.smhi.se/api/category/snow1g/version/1/";
  url += "geotype/point/lon/";
  url += String(cfg.lon, 5);
  url += "/lat/";
  url += String(cfg.lat, 5);
  url += "/data.json?parameters=air_temperature,wind_speed,probability_of_precipitation,";
  url += "precipitation_amount_mean,symbol_code";
  return url;
}

struct Point { time_t t; float temp, wind, risk, rain; int symbol; };
static Point points[MAX_POINTS];
static int pointCount = 0;
static WeatherSnapshot published;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static bool started = false;
static char created[32] = {};
static time_t lastDownloadedAt = 0;

float num(JsonVariantConst v) {
  if (v.isNull()) return NAN;
  float f = v.as<float>();
  return f == 9999.0f ? NAN : f;
}

time_t isoUtc(const char *s) {
  int y,m,d,h,mi,sec;
  if (!s || sscanf(s,"%d-%d-%dT%d:%d:%d",&y,&m,&d,&h,&mi,&sec)!=6) return 0;
  y -= m<=2;
  const int era=(y>=0?y:y-399)/400;
  const unsigned yoe=(unsigned)(y-era*400);
  const int mp=m+(m>2?-3:9);
  const unsigned doy=(153u*(unsigned)mp+2)/5+(unsigned)d-1;
  const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
  const int64_t days=(int64_t)era*146097+doe-719468;
  return (time_t)(days*86400+h*3600+mi*60+sec);
}

time_t localAt(time_t base, int days, int hour) {
  struct tm t;
  localtime_r(&base,&t);
  t.tm_mday += days;
  t.tm_hour=hour; t.tm_min=0; t.tm_sec=0; t.tm_isdst=-1;
  return mktime(&t);
}

void range(float value,float &lo,float &hi) {
  if(isnan(value)) return;
  if(isnan(lo)||value<lo) lo=value;
  if(isnan(hi)||value>hi) hi=value;
}
void maximum(float value,float &result) {
  if(!isnan(value)&&(isnan(result)||value>result)) result=value;
}
bool wet(int code) { return code>=8&&code<=27; }

WeatherSummary summarize(time_t start,time_t end) {
  WeatherSummary s;
  int counts[28] = {};
  float highestWet=-1;
  int wetCode=0;
  time_t middle=start+(end-start)/2;
  int64_t nearest=INT64_MAX;
  for(int i=0;i<pointCount;++i) {
    const Point &p=points[i];
    if(p.t<start||p.t>=end) continue;
    ++s.samples;
    range(p.temp,s.minTemp,s.maxTemp);
    maximum(p.wind,s.maxWind);
    maximum(p.risk,s.maxRisk);
    maximum(p.rain,s.maxRain);
    int64_t distance=llabs((long long)p.t-middle);
    if(!isnan(p.temp)&&distance<nearest) { s.centerTemp=p.temp; nearest=distance; }
    if(p.symbol>=1&&p.symbol<=27) ++counts[p.symbol];
    if(wet(p.symbol) && ((!isnan(p.rain)&&p.rain>=0.2f)||
                         (!isnan(p.risk)&&p.risk>=60.0f))) {
      float strength=(isnan(p.rain)?0:p.rain)*100+(isnan(p.risk)?0:p.risk);
      if(strength>highestWet) { highestWet=strength; wetCode=p.symbol; }
    }
  }
  if(wetCode) s.symbol=wetCode;
  else {
    int best=0;
    for(int c=1;c<=27;++c) if(counts[c]>best) { best=counts[c]; s.symbol=c; }
  }
  s.valid=s.samples>0;
  return s;
}

// Sex 4-timmarsfack som tillsammans tacker nasta 24 timmar.
// Temperatur, symbol och vind ar SAMMA timsteg. SMHI SNOW ger normalt
// timupplosning under de forsta dygnen. Nederborden summeras per fack.
void fillHourSlots(WeatherSnapshot &out, time_t now) {
  const time_t first = ((now + 3599) / 3600) * 3600;
  for (int i = 0; i < 6; ++i) {
    WeatherHourSample &slot = out.hours[i];
    slot.at = first + i * 4 * 3600;
    const time_t end = slot.at + 4 * 3600;
    const Point *nearest = nullptr;
    long long nearestDiff = 3601; // acceptera prognos inom en timme
    float rainSum = 0.0f;
    int rainSamples = 0;
    for (int j = 0; j < pointCount; ++j) {
      const Point &p = points[j];
      const long long diff = llabs((long long)p.t - (long long)slot.at);
      if (diff < nearestDiff && isfinite(p.temp)) {
        nearest = &p;
        nearestDiff = diff;
      }
      // SNOW:s intervallparametrar avser intervallet som slutar vid p.t.
      // Beakta bara tidssteg i (start, slut].
      if (p.t > slot.at && p.t <= end && isfinite(p.rain)) {
        rainSum += fmaxf(0.0f, p.rain);
        ++rainSamples;
      }
    }
    if (nearest) {
      slot.valid = true;
      slot.temperature = nearest->temp;
      slot.wind = nearest->wind;
      slot.symbol = nearest->symbol;
    }
    if (rainSamples) {
      slot.rainValid = true;
      slot.rainMm = rainSum;
    }
  }
}

void computeAndPublish() {
  const time_t now=time(nullptr);
  if(now<1700000000 || pointCount==0) return;
  WeatherSnapshot next;
  next.valid=true;
  next.fetchedAt=lastDownloadedAt;
  strlcpy(next.createdUtc,created,sizeof(next.createdUtc));
  struct tm local;
  localtime_r(&now,&local);
  const int h=local.tm_hour;
  if(h>=21) {
    strlcpy(next.periodName,"Nästa morgon",sizeof(next.periodName));
    next.periodStart=localAt(now,1,6); next.periodEnd=localAt(now,1,12);
  } else if(h<5) {
    strlcpy(next.periodName,"Morgonen",sizeof(next.periodName));
    next.periodStart=localAt(now,0,6); next.periodEnd=localAt(now,0,12);
  } else if(h<10) {
    strlcpy(next.periodName,"Dagen",sizeof(next.periodName));
    next.periodStart=now; next.periodEnd=localAt(now,0,16);
  } else if(h<16) {
    strlcpy(next.periodName,"Närmaste 8 timmarna",sizeof(next.periodName));
    next.periodStart=now; next.periodEnd=now+8*3600;
  } else {
    strlcpy(next.periodName,"Kvällen",sizeof(next.periodName));
    next.periodStart=now; next.periodEnd=localAt(now,0,21);
  }
  next.main=summarize(next.periodStart,next.periodEnd);
  for(int i=0;i<5;++i) {
    next.dayStarts[i]=localAt(now,i+1,0);
    next.days[i]=summarize(next.dayStarts[i],localAt(now,i+2,0));
  }
  fillHourSlots(next, now);
  portENTER_CRITICAL(&mux);
  published=next;
  portEXIT_CRITICAL(&mux);
  Serial.printf("SMHI: %d tidssteg, huvudprognos %s, %.1f C, ledigt heap %u\n",
                pointCount,next.periodName,next.main.centerTemp,ESP.getFreeHeap());
  for(int i=0;i<5;++i)
    Serial.printf("  dag %d: kod %d, %.1f / %.1f C (%d steg)\n",
       i+1,next.days[i].symbol,next.days[i].maxTemp,next.days[i].minTemp,next.days[i].samples);
}

bool fetch() {
  Serial.printf("SMHI före HTTPS: ledigt=%u, största block=%u byte\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  WiFiClientSecure client;
  // PROTOTYP: byt setInsecure mot certifikatverifiering före slutlig driftsättning.
  client.setInsecure();
  client.setTimeout(20);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(20000);
  const String url = forecastUrl();
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept-Encoding", "identity");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("SMHI HTTP: %d (%s)\n", status,
                  http.errorToString(status).c_str());
    http.end();
    return false;
  }

  // Den fullständiga timeSeries-arrayen får inte byggas upp som ett
  // ArduinoJson-dokument. LVGL + TLS lämnar för lite sammanhängande RAM.
  // I stället läser vi ETT objekt åt gången direkt från HTTP-strömmen.
  Stream &stream = http.getStream();
  stream.setTimeout(20000);
  auto readChar = [&stream]() -> int {
    char c;
    return stream.readBytes(&c, 1) == 1 ? (unsigned char)c : -1;
  };

  // SMHI:s SNOW1gv1-svar börjar med createdTime och har timeSeries därefter.
  // Metadata läses till en fast teckenbuffert, utan dynamiska JSON-allokeringar.
  char fetchedCreated[sizeof(created)] = {};
  if (!stream.find("\"createdTime\"") || !stream.find(":")) {
    Serial.println("SMHI JSON: createdTime saknas");
    http.end();
    return false;
  }
  int ch;
  do {
    ch = readChar();
  } while (ch >= 0 && ch != '"');
  if (ch != '"') {
    Serial.println("SMHI JSON: createdTime felaktig");
    http.end();
    return false;
  }
  size_t pos = 0;
  while ((ch = readChar()) >= 0 && ch != '"') {
    if (pos + 1 < sizeof(fetchedCreated)) fetchedCreated[pos++] = (char)ch;
  }
  if (ch != '"' || !stream.find("\"timeSeries\"") || !stream.find("[")) {
    Serial.println("SMHI JSON: timeSeries saknas");
    http.end();
    return false;
  }

  int count = 0;
  bool complete = false;
  static Point parsed[MAX_POINTS];  // Statisk: worker-stacken är begränsad.
  for (;;) {
    int next;
    // Hoppa över kommatecken/blanksteg fram till nästa JSON-objekt.
    do {
      next = readChar();
      if (next < 0) {
        Serial.println("SMHI JSON: avbruten HTTP-ström");
        http.end();
        return false;
      }
    } while (next == ',' || next == ' ' || next == '\r' ||
             next == '\n' || next == '\t');

    if (next == ']') { complete = true; break; }
    if (next != '{') {
      Serial.printf("SMHI JSON: oväntat tecken %d\n", next);
      http.end();
      return false;
    }

    // ArduinoJson måste även få den inledande '{'. Stream kan inte backas:
    // därför läser vi objektet med en liten fast buffert och klammerdjup.
    char object[768];
    size_t n = 0;
    object[n++] = '{';
    int depth = 1;
    bool quoted = false;
    bool escaped = false;
    while (depth > 0) {
      int c = readChar();
      if (c < 0 || n + 1 >= sizeof(object)) {
        Serial.println("SMHI JSON: tidssteg för långt eller avbrutet");
        http.end();
        return false;
      }
      object[n++] = (char)c;
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
      } else {
        if (c == '"') quoted = true;
        else if (c == '{') ++depth;
        else if (c == '}') --depth;
      }
    }
    object[n] = '\0';

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, object);
    if (error) {
      Serial.printf("SMHI JSON tidssteg: %s\n", error.c_str());
      http.end();
      return false;
    }
    if (count >= MAX_POINTS) {
      Serial.println("SMHI: fler tidssteg än bufferten tillåter");
      http.end();
      return false;
    }
    const time_t t = isoUtc(doc["time"] | "");
    if (!t) continue;
    JsonObjectConst data = doc["data"];
    Point &p = parsed[count++];
    p.t = t;
    p.temp = num(data["air_temperature"]);
    p.wind = num(data["wind_speed"]);
    p.risk = num(data["probability_of_precipitation"]);
    p.rain = num(data["precipitation_amount_mean"]);
    p.symbol = data["symbol_code"].isNull() ? 0 : data["symbol_code"].as<int>();
  }
  http.end();
  if (!complete || count == 0) return false;
  memcpy(points, parsed, count * sizeof(Point));
  pointCount = count;
  strlcpy(created, fetchedCreated, sizeof(created));
  Serial.printf("SMHI JSON OK: %d tidssteg, ledigt heap %u byte\n",
                pointCount, ESP.getFreeHeap());
  return true;
}

void worker(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  const WxSettings &cfg = wx_settings();
  WiFi.persistent(false);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  uint32_t lastConnectAttempt=millis();
  uint32_t lastSuccessfulFetch=0;
  uint32_t lastFetchAttempt=0;
  uint32_t lastRecompute=0;
  bool clockConfigured=false;
  for(;;) {
    uint32_t nowMs=millis();
    if(WiFi.status()!=WL_CONNECTED) {
      if(nowMs-lastConnectAttempt>=15000) {
        WiFi.reconnect(); lastConnectAttempt=nowMs;
        Serial.println("SMHI: försöker ansluta WiFi...");
      }
    } else {
      if(!clockConfigured) {
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","se.pool.ntp.org","pool.ntp.org");
        clockConfigured=true;
        Serial.println("SMHI: WiFi anslutet, NTP startat");
      }
      if(time(nullptr)>1700000000) {
        // Försök direkt, därefter högst var femte minut vid fel och var 30:e minut vid framgång.
        if((lastFetchAttempt==0 || nowMs-lastFetchAttempt>=5UL*60UL*1000UL) &&
           (lastSuccessfulFetch==0 || nowMs-lastSuccessfulFetch>=REFRESH_MS)) {
          lastFetchAttempt=nowMs;
          if(fetch()) { lastSuccessfulFetch=millis(); lastDownloadedAt=time(nullptr); computeAndPublish(); }
          else Serial.println("SMHI: hämtningen misslyckades; försöker senare");
        }
        if(pointCount>0 && (lastRecompute==0 || nowMs-lastRecompute>=60000)) {
          computeAndPublish(); lastRecompute=nowMs;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
} // namespace

const char *weather_name(int code) {
  static const char *names[]={"Saknas","Klart","Nästan klart","Växlande molnighet","Halvklart",
    "Molnigt","Mulet","Dimma","Lätta regnskurar","Regnskurar","Kraftiga regnskurar",
    "Åska","Lätt snöblandat regn","Snöblandat regn","Kraftigt snöblandat regn",
    "Lätta snöbyar","Snöbyar","Kraftiga snöbyar","Lätt regn","Regn","Kraftigt regn",
    "Åska","Lätt snöblandat regn","Snöblandat regn","Kraftigt snöblandat regn",
    "Lätt snöfall","Snöfall","Kraftigt snöfall"};
  return code>=1 && code<=27 ? names[code] : "Okänt";
}

void weather_begin() {
  if(started) return;
  started=true;
  BaseType_t ok=xTaskCreatePinnedToCore(worker,"smhi",12288,nullptr,1,nullptr,0);
  if(ok!=pdPASS) Serial.println("SMHI: kunde inte skapa worker-task");
}

bool weather_get(WeatherSnapshot &out) {
  portENTER_CRITICAL(&mux);
  out=published;
  portEXIT_CRITICAL(&mux);
  return out.valid;
}

void weather_local_clock(char *out,size_t len) {
  if(!len) return;
  time_t now=time(nullptr);
  if(now<1700000000) { strlcpy(out,"--:--",len); return; }
  struct tm local;
  localtime_r(&now,&local);
  strftime(out,len,"%H:%M",&local);
}
