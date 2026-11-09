#pragma once
// Anropas från Arduino loop(), aldrig från SMHI:s bakgrundstråd.
void weather_ui_poll();
