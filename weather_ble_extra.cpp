// Experimental extras inside the Improv service: indexed SSID list and location upload.
// WiFi credentials still use the STANDARD Improv RPC 0x01.
#include "weather_ble_extra.h"
#include <string>
#include <cmath>

// Deliberately outside the standard Improv UUID range.
// Clients need the same two UUIDs; the custom characteristics are not Improv standard.
static const char *NET_UUID = "c0161234-4455-4f66-8777-123456789001";
static const char *LOC_UUID = "c0161234-4455-4f66-8777-123456789002";

static std::vector<WxNetwork> networks;
static std::string locationBuffer;
static String pendingCity;
static double pendingLat = 0, pendingLon = 0;
static bool locationReady = false;
static NimBLECharacteristic *netChar = nullptr, *locationChar = nullptr;

void wxSetNetworks(const std::vector<WxNetwork>& nets) { networks = nets; }

bool wxGetPendingLocation(String& city, double& lat, double& lon) {
  if (!locationReady) return false;
  city = pendingCity;
  lat = pendingLat;
  lon = pendingLon;
  return true;
}

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
    (void)info;
    const std::string v = c->getValue();
    if (c == netChar) {
      // Write [SSID 1-based index, 16-byte chunk number], then read this characteristic.
      // Reply <=20 bytes: [count, RSSI(int8), secure, full_ssid_len, up_to_16_SSID_bytes].
      // Index=0 returns the number of discoverable networks.
      uint8_t ix = v.size() ? static_cast<uint8_t>(v[0]) : 0;
      uint8_t part = v.size() > 1 ? static_cast<uint8_t>(v[1]) : 0;
      uint8_t response[20] = { static_cast<uint8_t>(networks.size()), 0, 0, 0 };
      size_t bytes = 4;
      if (ix >= 1 && ix <= networks.size()) {
        const auto &n = networks[ix - 1];
        response[1] = static_cast<uint8_t>(static_cast<int8_t>(std::max(-127, std::min(0, n.rssi))));
        response[2] = n.secured ? 1 : 0;
        const size_t length = std::min<size_t>(32, n.ssid.length());
        response[3] = static_cast<uint8_t>(length);
        const size_t offset = static_cast<size_t>(part) * 16;
        if (offset < length) {
          const size_t take = std::min<size_t>(16, length - offset);
          memcpy(response + 4, n.ssid.c_str() + offset, take);
          bytes += take;
        }
      }
      c->setValue(response, bytes);
      return;
    }
    if (c == locationChar) {
      // Command 1: reset; 2: append up to 18 UTF-8 bytes; 3: validate+commit.
      // Format: <ortnamn>\n<lat>\n<lon>. Never store credentials here.
      uint8_t status = 0;
      if (v.empty()) status = 1;
      else if (static_cast<uint8_t>(v[0]) == 1) {
        locationBuffer.clear(); locationReady = false;
      } else if (static_cast<uint8_t>(v[0]) == 2) {
        if (locationBuffer.size() + v.size() - 1 > 160) status = 2;
        else locationBuffer.append(v.data() + 1, v.size() - 1);
      } else if (static_cast<uint8_t>(v[0]) == 3) {
        const size_t a = locationBuffer.find('\n');
        const size_t b = a == std::string::npos ? a : locationBuffer.find('\n', a + 1);
        if (a == std::string::npos || b == std::string::npos) status = 3;
        else {
          const String city(locationBuffer.substr(0, a).c_str());
          const double lat = atof(locationBuffer.substr(a + 1, b - a - 1).c_str());
          const double lon = atof(locationBuffer.substr(b + 1).c_str());
          if (city.isEmpty() || city.length() > 79 || !std::isfinite(lat) || !std::isfinite(lon)
              || lat < 54.0 || lat > 70.0 || lon < 10.0 || lon > 25.0) status = 4;
          else {
            pendingCity = city;
            pendingLat = lat;
            pendingLon = lon;
            locationReady = true;
            Serial.printf("ORT: mottagen via BLE: %s (%.5f, %.5f); invantar WiFi\n", city.c_str(), lat, lon);
          }
        }
      } else status = 5;
      c->setValue(&status, 1);
    }
  }
};

static ConfigCallbacks callbacks;
void wxBLEInitExtra(NimBLEService* service) {
  netChar = service->createCharacteristic(NET_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  locationChar = service->createCharacteristic(LOC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  netChar->setCallbacks(&callbacks);
  locationChar->setCallbacks(&callbacks);
  uint8_t zero = 0;
  netChar->setValue(&zero, 1);
  locationChar->setValue(&zero, 1);
}
