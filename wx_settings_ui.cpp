#include "wx_settings_ui.h"
#include "wx_settings.h"
#include <Arduino.h>
#include <lvgl.h>
#include "src/ui/ui.h"

namespace {
static lv_obj_t *popup = nullptr;

void closePopup() {
  if (popup) {
    lv_obj_del(popup);
    popup = nullptr;
  }
}

void cancelClicked(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) closePopup();
}

void continueClicked(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!wx_request_setup_restart()) {
    Serial.println("WX: NVS kunde inte spara begaran om konfigurering");
    closePopup();
    return;
  }
  closePopup(); // Main loop restarts after LVGL has returned.
}

lv_obj_t *choice(lv_obj_t *parent, const char *label, bool accept) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, 116, 35);
  lv_obj_align(button, accept ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT,
               accept ? -10 : 10, -9);
  lv_obj_set_style_radius(button, 5, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(accept ? 0x174B83 : 0x52738D), 0);
  lv_obj_t *caption = lv_label_create(button);
  lv_label_set_text(caption, label);
  lv_obj_set_style_text_font(caption, &ui_font_my_barlow_bold_14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(caption,lv_color_hex(0xFFFFFF),0);
  lv_obj_center(caption);
  lv_obj_add_event_cb(button, accept ? continueClicked : cancelClicked,
                      LV_EVENT_CLICKED, NULL);
  return button;
}

void gearClicked(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || popup) return;
  // LV_USE_MSGBOX=0 in LCD Wizard's lv_conf.h, so use normal LVGL widgets.
  popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup, 286, 150);
  lv_obj_center(popup);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(popup,lv_color_hex(0xE6F1F9),0);
  lv_obj_set_style_border_width(popup,2,0);
  lv_obj_set_style_border_color(popup,lv_color_hex(0x174B83),0);
  lv_obj_set_style_radius(popup,8,0);
  // The top LVGL layer does not inherit LCD Wizard's Barlow fonts.
  // Explicitly use the already-bundled font with Swedish glyphs.
  lv_obj_set_style_text_font(popup, &ui_font_my_barlow_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_t *title=lv_label_create(popup);
  lv_label_set_text(title,"Konfigurera Väderdisplay");
  lv_obj_set_style_text_font(title, &ui_font_my_barlow_bold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t *prompt=lv_label_create(popup);
  lv_label_set_text(prompt,"Ändra WiFi eller ort?");
  lv_obj_set_style_text_font(prompt, &ui_font_my_barlow_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(prompt, LV_ALIGN_CENTER, 0, -11);
  choice(popup,"Avbryt",false);
  choice(popup,"Fortsätt",true);
}
} // namespace

void wx_settings_ui_install() {
  // Kugghjulet är nu skapat och placerat av LCD Wizard.
  // Registrera händelsen där, och skapa INTE ett andra kugghjul.
  if (!ui_Button1) {
    Serial.println("WX: LCD Wizard-kugghjulet saknas");
    return;
  }
  // LCD Wizard export: ui_Button1 left border has no explicit color.
  // Match the intended white separator from the designer.
  lv_obj_set_style_border_color(ui_Button1, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_opa(ui_Button1, LV_OPA_COVER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(ui_Button1, gearClicked, LV_EVENT_CLICKED, NULL);
}
