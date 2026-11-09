#pragma once
#include <Arduino.h>
#include <vector>
#include <NimBLEDevice.h>

struct WxNetwork {
  String ssid;
  int rssi;
  bool secured;
};

// These are local, test-only extensions to the same Improv BLE service.
void wxSetNetworks(const std::vector<WxNetwork>& nets);
void wxBLEInitExtra(NimBLEService* service);
bool wxGetPendingLocation(String& city, double& lat, double& lon);
