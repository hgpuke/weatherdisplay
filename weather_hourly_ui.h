#pragma once
// Dygnsprognos byggs dynamiskt i LVGL utan LCD Wizard-export.
// Anropas enbart i LVGL/Arduino-huvudtraden.
void weather_hourly_ui_install();
void weather_hourly_ui_poll();
