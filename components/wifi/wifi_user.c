/**
 * @file    wifi_user.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"  
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "sys/time.h"
#include "time.h"
#include <string.h>
 
#include "esp_event.h"
#include "wifi_sock.h"
#include "wifi_init.h"
#include "wifi_mqtt.h"

#include "wifi_user.h"

#define TAG  "wifi_user"
 
int app_upper_cloud_write(const char *data, uint16_t len)
{
    #if APP_CONFIG_MQTT_ENABLE
    return app_mqtt_publish_cloud(data, len);
    #else 
    return tcp_client_write((uint8_t *)data, len);
    #endif
}


/**
 * @brief  发送给云端的JSON格式
 * 
 * @param addr ： 设备地址
 * @param opcode ： 键值名
 * @param value ：键值
 * @param size ：键值大小，如果size = 0为字符串格式，否则为：数组格式
 * @return true 
 * @return false 
 */
bool app_upper_cloud_format(uint16_t dst_addr, const char *mid, const char *opcode, void *value, uint8_t size)
{
    #if APP_CONFIG_MQTT_ENABLE
    if (mqtt_connect_status(0) == false) return 0;   
    #else 
    if (tcp_client_connect_status() == false) return 0;
    #endif

    /* 2、拼接成JSON {"addr":<MAC>,"opcode":<DATA>} */ 
    cJSON *root = cJSON_CreateObject(); 
    if (root == NULL) return 0;
 
    char uniaddr[7];
    sprintf(uniaddr, "0x%04X", dst_addr); 
    cJSON_AddItemToObject(root, "addr", cJSON_CreateString(uniaddr) );
    #if APP_CONFIG_MQTT_ENABLE
    cJSON_AddItemToObject(root, "mid", cJSON_CreateString(mid) );
    #endif
 
    #define ARR_SIZE   8 
    if (size > 0 && size < ARR_SIZE) {  // 数组格式
        int arrint[ARR_SIZE];
        for (uint8_t i = 0; i < size; i++) {
            arrint[i] = ((uint8_t *)value)[i];  // 把uint8_t 转成 int 类型
        }
        cJSON_AddItemToObject(root, opcode, cJSON_CreateIntArray(arrint, size));
    } else {  // 字符串格式
        cJSON_AddItemToObject(root, opcode, cJSON_CreateString((char *)value) );
    } 
 
    char *cjson_data = cJSON_PrintUnformatted(root);
    if (cjson_data == NULL) return 0; 
    uint16_t data_len = strlen(cjson_data);

    #ifdef APP_USER_DEBUG_ENABLE     
    ESP_LOGI(TAG, "app_wifi_sock_write = %s | %d", cjson_data, data_len);
    #endif   
 
    app_upper_cloud_write(cjson_data, data_len);

    cJSON_free(cjson_data);
    cJSON_Delete(root);
    root = NULL;

    return 1;
}
 
void app_wifi_user_init(void)
{
    
}
/* END...................................................................................*/
