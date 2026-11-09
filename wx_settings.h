#pragma once
#include <Arduino.h>

// NVS only, without hard-coded credentials in source files.
// Two committed slots: a power failure during save does not destroy older settings.
struct WxSettings {
  String ssid;
  String password;
  String city;
  double lat = 0.0;
  double lon = 0.0;
  uint32_t revision = 0;
};

const WxSettings &wx_settings();
bool wx_config_is_valid();
bool wx_save_settings(const char *ssid, const char *password,
                      const String &city, double lat, double lon);
bool wx_boot_needs_setup();
bool wx_request_setup_restart();
bool wx_restart_to_setup_requested();
