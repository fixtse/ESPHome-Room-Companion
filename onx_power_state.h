#pragma once
#include "esphome/core/log.h"
#ifdef USE_ESP32
#include <esp_wifi.h>
#endif

namespace onx_power {
inline bool sleeping=false,waking=false,wake_press=false;
#ifdef USE_ESP32
inline wifi_ps_type_t previous_radio_mode=WIFI_PS_NONE;
inline bool radio_saved=false;
inline wifi_ps_type_t ota_radio_mode = WIFI_PS_NONE;
inline bool ota_radio_saved = false;
#endif
inline void begin_ota() {
#ifdef USE_ESP32
  if (ota_radio_saved) return;
  ota_radio_saved = esp_wifi_get_ps(&ota_radio_mode) == ESP_OK;
  if (ota_radio_saved && esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK)
    ESP_LOGW("low_power", "Could not disable Wi-Fi modem sleep for OTA");
#endif
}
inline void end_ota() {
#ifdef USE_ESP32
  if (ota_radio_saved && esp_wifi_set_ps(ota_radio_mode) != ESP_OK)
    ESP_LOGW("low_power", "Could not restore Wi-Fi modem sleep after OTA");
  ota_radio_saved = false;
#endif
}
inline void apply_radio_sleep() {
#ifdef USE_ESP32
  if (ota_radio_saved) return;
  if(!radio_saved)radio_saved=esp_wifi_get_ps(&previous_radio_mode)==ESP_OK;
  if(esp_wifi_set_ps(WIFI_PS_MIN_MODEM)!=ESP_OK)
    ESP_LOGW("low_power","Wi-Fi modem power saving could not be applied");
#endif
}
inline void enter() {sleeping=true;waking=false;apply_radio_sleep();}
inline void restore_radio() {
  sleeping=false;
#ifdef USE_ESP32
  if (radio_saved) {
    if (ota_radio_saved)
      ota_radio_mode = previous_radio_mode;
    else if (esp_wifi_set_ps(previous_radio_mode) != ESP_OK)
      ESP_LOGW("low_power", "Wi-Fi power saving mode could not be restored");
  }
  radio_saved=false;
#endif
}
}
