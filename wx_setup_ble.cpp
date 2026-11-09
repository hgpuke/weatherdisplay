#include "wx_setup_ble.h"
#include "wx_settings.h"
#include "weather_ble_extra.h"
#include "qr_data.h"
#include "wx_qr_title.h"
#include "lcd_pins.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiBLE.h>
#include <TFT_eSPI.h>
#include <algorithm>
#include <vector>

// The TFT_eSPI object in Weatherdisplay.ino is reused, no second SPI display.
extern TFT_eSPI tft;
namespace {
ImprovWiFiBLE improv;
volatile bool savedAndConnected = false;
volatile bool invalidLocation = false;
volatile bool failedStorage = false;

void drawQR() {
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LCD_BL_ON_LEVEL);
  tft.begin();
  tft.setRotation(LCD_ROTATION);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  // Den vanliga TFT_eSPI-fonten saknar Ä; rita hela rubriken som
  // ett 1-bits raster i flashminnet, utan att starta LVGL/Bluetooth-RAM.
  tft.drawBitmap((SCREEN_WIDTH-WX_QR_TITLE_W)/2, 3,
                 WX_QR_TITLE_BITMAP, WX_QR_TITLE_W, WX_QR_TITLE_H, TFT_WHITE);
  constexpr int quiet=4;
  const int scale=std::min(5,188/(int(QR_MODULES)+2*quiet));
  const int side=(int(QR_MODULES)+2*quiet)*scale;
  const int x0=(SCREEN_WIDTH-side)/2;
  const int y0=24;
  tft.fillRect(x0,y0,side,side,TFT_WHITE);
  tft.startWrite();
  for(unsigned y=0;y<QR_MODULES;++y) {
    for(unsigned x=0;x<QR_MODULES;++x) {
      const unsigned k=y*QR_MODULES+x;
      if ((QR_DATA[k/8]>>(7-k%8))&1u)
        tft.fillRect(x0+(int(x)+quiet)*scale,y0+(int(y)+quiet)*scale,
                     scale,scale,TFT_BLACK);
    }
  }
  tft.endWrite();
  tft.drawCentreString("Skanna QR-koden",160,216,2);
  Serial.printf("QR: %s, %d x %d pixlar\n",WX_QR_URL,side,side);
}

std::vector<WxNetwork> scanWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  const int n=WiFi.scanNetworks(false,true);
  std::vector<WxNetwork> found;
  for(int i=0;i<n;++i) {
    String ssid=WiFi.SSID(i);
    if(ssid.isEmpty()) continue;
    const int rssi=WiFi.RSSI(i);
    const bool secure=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
    bool duplicate=false;
    for(auto &e:found) if(e.ssid==ssid) {
      if(rssi>e.rssi) {e.rssi=rssi; e.secured=secure;}
      duplicate=true;
      break;
    }
    if(!duplicate) found.push_back({ssid,rssi,secure});
  }
  WiFi.scanDelete();
  std::sort(found.begin(),found.end(),[](const WxNetwork&a,const WxNetwork&b) {
    return a.rssi>b.rssi;
  });
  if(found.size()>50) found.resize(50);
  Serial.printf("WiFi: %u synliga 2,4 GHz-natverk\n",(unsigned)found.size());
  return found;
}

bool connectToWifi(const char *ssid,const char *pass) {
  // Called on NimBLE task. Do not use LVGL or restart in this callback.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false,false);
  WiFi.begin(ssid,pass);
  const uint32_t start=millis();
  while(millis()-start<18000) {
    if(WiFi.status()==WL_CONNECTED) {
      Serial.printf("WiFi: anslutet till %s, IP %s\n",ssid,
                    WiFi.localIP().toString().c_str());
      return true;
    }
    delay(150);
  }
  Serial.println("WiFi: anslutningen misslyckades; prova nytt losenord");
  return false;
}

void onConnected(const char *ssid,const char *pass) {
  String city;
  double lat=0,lon=0;
  if(!wxGetPendingLocation(city,lat,lon)) {
    invalidLocation=true;
    Serial.println("WX: ORT SAKNAS. Ingen ny konfiguration sparas.");
    return;
  }
  if(!wx_save_settings(ssid,pass,city,lat,lon)) {
    failedStorage=true;
    Serial.println("WX: NVS-SPARANDE MISSLYCKADES. Tidigare konfiguration bevarad.");
    return;
  }
  Serial.printf("WX: sparat WiFi %s och ort %s (%.5f, %.5f)\n",
                ssid,city.c_str(),lat,lon);
  savedAndConnected=true;
}
} // namespace

void wx_run_ble_setup() {
  Serial.println("WX: BLE-konfigurationslage, LVGL och SMHI ar AV");
  drawQR();
  Serial.printf("WX: heap fore BLE %u\n",ESP.getFreeHeap());
  wxSetNetworks(scanWifi());
  WiFi.setAutoReconnect(false);
  improv.setCustomConnectWiFi(connectToWifi);
  improv.onImprovConnected(onConnected);
  improv.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32,
                       "Weatherdisplay","1.0","Vaderdisplay-konfig","");
  Serial.printf("WX: BLE aktivt, heap %u\n",ESP.getFreeHeap());
  uint32_t successSince=0;
  for(;;) {
    if(savedAndConnected) {
      if(!successSince) {
        successSince=millis();
        Serial.println("WX: klart. Startar om till normal vaderdisplay inom kort.");
      }
      if(millis()-successSince>=3000) ESP.restart();
    }
    static uint32_t last=0;
    if(millis()-last>30000) {
      last=millis();
      Serial.printf("WX: vantar pa installning, heap %u\n",ESP.getFreeHeap());
      if(invalidLocation) Serial.println("WX: wifi lyckades men orten skickades inte. Skanna om for att prova igen.");
      if(failedStorage) Serial.println("WX: wifi lyckades men installningarna kunde inte sparas.");
    }
    delay(50);
  }
}
