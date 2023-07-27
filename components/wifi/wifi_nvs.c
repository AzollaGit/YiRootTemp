/**
 * @file    wifi_nvs.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#include <string.h>
#include <stdio.h>

#include "hal_nvs.h" 

#include "wifi_nvs.h"

 
// 存储WIFI信息
// NVS_READONLY,  /*!< Read only */
// NVS_READWRITE  /*!< Read and write */
// nvs_wifi_handle(&nvs_wifi, NVS_READONLY);
esp_err_t nvs_wifi_handle(nvs_wifi_t *nvs, nvs_open_mode_t nvs_open_mode)
{
    return nvs_blob_handle((nvs_wifi_t *)nvs, sizeof(nvs_wifi_t), "wifi_key", nvs_open_mode);
}
// 复位
void nvs_wifi_reset(void)
{
    nvs_wifi_t nvs = { 0 };
    nvs_wifi_handle(&nvs, NVS_READWRITE);   // 写配置参数
}

esp_err_t nvs_mqtt_handle(nvs_mqtt_t *nvs, nvs_open_mode_t nvs_open_mode)
{
   return nvs_blob_handle((nvs_mqtt_t *)nvs, sizeof(nvs_mqtt_t), "mqtt_key", nvs_open_mode);
}

void nvs_mqtt_reset(void)
{
    nvs_mqtt_t nvs = { 0 };
    nvs_mqtt_handle(&nvs, NVS_READWRITE);  
}
 
// NVS_READONLY,  /*!< Read only */
// NVS_READWRITE  /*!< Read and write */
esp_err_t nvs_sntp_handle(struct tm *nvs, nvs_open_mode_t nvs_open_mode)
{
    return nvs_blob_handle((struct tm *)nvs, sizeof(struct tm), "tm_key", nvs_open_mode);
}

