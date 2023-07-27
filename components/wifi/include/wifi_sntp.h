/**
 * @file    wifi_sntp.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   using LwIP SNTP module and time functions
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __WIFI_SNTP_H__
#define __WIFI_SNTP_H__

#include "esp_sntp.h"

void wifi_sntp_init(void);

void sntp_sync_time_custom(time_t timestamp);

struct tm* sntp_get_time(void);

void sntp_print_time(void);
 
#endif /**< __WIFI_SNTP_H__ */
