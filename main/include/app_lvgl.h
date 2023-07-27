#ifndef __APP_LVGL_H
#define __APP_LVGL_H

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#include "wifi_init.h"
#include "wifi_ota.h"
#include "wifi_sntp.h" 

#include "audio_board_init.h"
 
typedef struct {
    uint8_t windows;
    lv_event_code_t event;
    uint8_t value[6];
    uint8_t len;
} lv_app_event_t;

typedef void (*lv_event_callback_t)(lv_app_event_t);
void lv_event_register_callback(lv_event_callback_t cb);
 
void app_lvgl_init(void);

void lv_time_handle(struct tm *sntp);
void lv_ble_icon_switch(bool onoff);
void lv_wifi_icon_switch(bool onoff);
void lv_server_icon_switch(bool onoff); 


void app_music_play_ctrl(bool play, audio_ctrl_t ctrl);
void app_music_play_vol(uint8_t vol);

void app_lv_scene_close(void);
void app_lv_scene_create(const char *name);

void app_lv_ledc_set_onoff(bool onoff);
void app_lv_ledc_set_temp(uint8_t value);
void app_lv_ledc_set_bright(uint8_t value);
void app_lv_ledc_set_hsv(uint16_t h, uint8_t s, uint8_t v);

#endif
