/**
 * @file    wifi_nvs.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __WIFI_NVS_H__
#define __WIFI_NVS_H__

#include "nvs.h"
#include "esp_sntp.h"
 
typedef struct {
#define WIFI_CONFIG_NULL   0x00   // 未配网
#define WIFI_CONFIG_WIFI   0x01   // WIFI上网   
#define WIFI_CONFIG_ETH    0x02   // ETH上网
    uint8_t status;             /**< config status. */ 
    char ssid[32];              /**< SSID of ESP32 soft-AP. If ssid_len field is 0, this must be a Null terminated string. Otherwise, length is set according to ssid_len. */
    char password[32];          /**< Password of ESP32 soft-AP. */
} nvs_wifi_t;

typedef struct {
#define MQTT_CONFIG_NULL 0x00   // 未配网
#define MQTT_CONFIG_OK   0x01   // 配网MQTT      
    uint8_t status;             /**<config status. */ 
    char host[32];              /**< MQTT host. */
    uint16_t port;              /**< MQTT port. */ 
    char  username[32];         /**< MQTT user name. */
    char  userword[16];         /**< MQTT user password. */
    uint8_t qos;                /**< MQTT user qos. */
} nvs_mqtt_t;

esp_err_t nvs_wifi_handle(nvs_wifi_t *nvs, nvs_open_mode_t nvs_open_mode);
esp_err_t nvs_mqtt_handle(nvs_mqtt_t *nvs, nvs_open_mode_t nvs_open_mode);
void nvs_wifi_reset(void);
void nvs_mqtt_reset(void);

esp_err_t nvs_sntp_handle(struct tm *nvs, nvs_open_mode_t nvs_open_mode);
 
#endif  /*__WIFI_NVS_H__ END.*/
