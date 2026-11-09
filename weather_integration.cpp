// SMHI integreras via LCD Wizards user_setup/user_loop; navigation kommer fran exporten.
#include <Arduino.h>
#include <lvgl.h>
#include "weather_engine.h"
#include "weather_ui.h"
#include "weather_hourly_ui.h"
#include "wx_settings_ui.h"
#include "src/ui/ui.h"

void user_setup(void) {
    Serial.printf("SMHI efter ui_init: heap=%u, maxblock=%u byte\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    wx_settings_ui_install();
    weather_hourly_ui_install();
    weather_begin();
}
void user_loop(void) { weather_ui_poll(); weather_hourly_ui_poll(); }
