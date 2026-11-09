#include "wx_settings.h"
#include <Preferences.h>
#include <math.h>

namespace {
WxSettings active;
bool ready = false;
bool asked = false;
static const char *names[] = {"wxslot0", "wxslot1"};

bool valid(const WxSettings &s) {
  // ESP32 WiFi only supports SSIDs <=32 octets; open networks can have blank password.
  return s.ssid.length() > 0 && s.ssid.length() <= 32 &&
         s.password.length() <= 64 && s.city.length() > 0 && s.city.length() <= 79 &&
         isfinite(s.lat) && isfinite(s.lon) &&
         s.lat >= 54.0 && s.lat <= 70.0 && s.lon >= 10.0 && s.lon <= 25.0;
}

bool readSlot(unsigned slot, WxSettings &out) {
  Preferences p;
  if (!p.begin(names[slot], true)) return false;
  if (!p.getBool("valid",false)) { p.end(); return false; }
  WxSettings s;
  s.ssid = p.getString("ssid","");
  s.password = p.getString("pass","");
  s.city = p.getString("city","");
  s.lat = p.getDouble("lat",0.0);
  s.lon = p.getDouble("lon",0.0);
  s.revision = p.getUInt("rev",0);
  p.end();
  if (!valid(s) || s.revision == 0) return false;
  out = s;
  return true;
}

void loadConfig() {
  WxSettings a, b;
  const bool va = readSlot(0,a), vb = readSlot(1,b);
  ready = va || vb;
  if (!ready) return;
  // Sequence comparisons tolerate wraparound, though realistic wear levels never reach it.
  active = (!va ? b : (!vb ? a : (int32_t)(a.revision - b.revision) > 0 ? a : b));
}
} // namespace

const WxSettings &wx_settings() { return active; }
bool wx_config_is_valid() { return ready; }

bool wx_save_settings(const char *ssid, const char *password,
                      const String &city, double lat, double lon) {
  WxSettings candidate;
  candidate.ssid = ssid ? ssid : "";
  candidate.password = password ? password : "";
  candidate.city = city;
  candidate.lat = lat;
  candidate.lon = lon;
  candidate.revision = ready ? active.revision + 1 : 1;
  if (!valid(candidate)) return false;
  WxSettings existing0, existing1;
  bool has0=readSlot(0,existing0), has1=readSlot(1,existing1);
  unsigned target = !has0 ? 0 : !has1 ? 1 :
      (int32_t)(existing0.revision - existing1.revision) > 0 ? 1 : 0;
  Preferences p;
  if(!p.begin(names[target],false)) return false;
  // 'valid' is committed last. Incomplete writes cannot be considered usable.
  p.putBool("valid",false);
  bool ok = p.putString("ssid",candidate.ssid) > 0;
  ok = p.putString("pass",candidate.password) > 0 && ok;
  ok = p.putString("city",candidate.city) > 0 && ok;
  ok = p.putDouble("lat",candidate.lat) > 0 && ok;
  ok = p.putDouble("lon",candidate.lon) > 0 && ok;
  ok = p.putUInt("rev",candidate.revision) > 0 && ok;
  if (ok) ok = p.putBool("valid",true) > 0;
  p.end();
  if (!ok) return false;
  WxSettings verify;
  if (!readSlot(target,verify) || verify.ssid != candidate.ssid ||
      verify.password != candidate.password || verify.city != candidate.city ||
      verify.lat != candidate.lat || verify.lon != candidate.lon) return false;
  active = candidate;
  ready = true;
  return true;
}

bool wx_boot_needs_setup() {
  loadConfig();
  Preferences p;
  bool requested = false;
  if (p.begin("wxmode",false)) {
    requested = p.getBool("setup",false);
    if (requested) p.putBool("setup",false);
    p.end();
  }
  Serial.printf("WX: NVS-konfiguration %s, installationslage %s\n",
                ready?"finns":"saknas",(!ready||requested)?"JA":"nej");
  return !ready || requested;
}

bool wx_request_setup_restart() {
  Preferences p;
  if (!p.begin("wxmode",false)) return false;
  const bool ok=p.putBool("setup",true) > 0;
  p.end();
  if (ok) asked = true;
  return ok;
}

bool wx_restart_to_setup_requested() { return asked; }
