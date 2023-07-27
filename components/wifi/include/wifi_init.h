/**
 * @file    wifi_init.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2023-02-06
 * 
 * @copyright Copyright (c) 2023
 * */
#ifndef __WIFI_INIT_H__
#define __WIFI_INIT_H__

#include "mac_utils.h"
 
typedef union {
    uint8_t ip4[4];
    uint32_t ip;
} ip4_info_t;

void app_wifi_init(const char *ssid, const char *pswd);

int wifi_sta_reconnect(const char *ssid, const char *password);

bool wifi_connect_status(uint32_t wait_time);

uint32_t wifi_sta_ip_addr(void);

bool wifi_get_cfg_status(void);

#endif  /*__WIFI_INIT_H__ END.*/