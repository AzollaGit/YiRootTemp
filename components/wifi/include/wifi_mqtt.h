/**
 * @file    wifi_mqtt.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __WIFI_MQTT_H__
#define __WIFI_MQTT_H__

#include "cJSON.h"

#include "wifi_nvs.h"

#define MQTT_RX_BUFF_SIZE      1024
#define MQTT_TX_BUFF_SIZE      1024

typedef struct {
    char cloud[26];
    char local[26];
    char sntp[17];
    uint8_t qos;
} mqtt_topic_t;

typedef struct {
    uint8_t   *data;
    uint16_t  len;
} mqtt_data_t;

typedef void (*mqtt_callback_t)(mqtt_data_t);

void mqtt_subscribe_register_callback(mqtt_callback_t callback_func);
 
void app_mqtt_init(nvs_mqtt_t *nvs_mqtt);

bool app_mqtt_client_reconnect(nvs_mqtt_t nvs_mqtt);

int app_mqtt_publish_topo(const char *data, uint16_t len);

int app_mqtt_publish_cloud(const char *data, uint16_t len);
 
bool mqtt_connect_status(uint32_t wait_time);

void app_mqtt_client_disconnect(void);

#endif  /*__WIFI_MQTT_H__ END. */

 

