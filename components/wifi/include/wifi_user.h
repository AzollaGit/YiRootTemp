/**
 * @file    wifi_user.h
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __WIFI_USER_H__
#define __WIFI_USER_H__

#include "nvs.h"
 
#include "wifi_sntp.h"

#define APP_CONFIG_MQTT_ENABLE    1 // 1: 使能MQTT; 0: TCP/UDP
 
//=============================================================

void app_wifi_user_init(void);

void app_mwifi_update_time(void);

int app_upper_cloud_write(const char *data, uint16_t len);

bool app_upper_cloud_format(uint16_t dst_addr, const char *mid, const char *opcode, void *value, uint8_t size);

uint8_t sntp_str_systime(char *timestr);
 
#endif  /*__WIFI_USER_H__ END.*/
/* END...................................................................................*/