/**
 * @file    wifi_mqtt.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-10-10
 * 
 * @copyright Copyright (c) 2022
 * */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#include "wifi_mqtt.h"
#include "wifi_init.h"
 
#define TAG   "wifi_mqtt"

// #define WIFI_MQTT_DEBUG_ENABLE  1

/** Config APP MQTT. */ 
// 内网：192.168.0.101 
// 主机：iot.yiree.com.cn
// 端口：1883
// 账号：gateway
// 密码：d63a52ff9b25
#if 0
豪江电机WIFI控制盒:
    info://112.124.38.58:1883
    username：haojiang
    password：yirui@2022
后台: 
    http://112.124.38.58:18083/#/  admin public  
    http://112.124.38.58:1883
#endif
 
static esp_mqtt_client_handle_t mqtt_handle;

static mqtt_topic_t mqtt_topic; 

// #define MQTT_BROKEY_ADDR_URL   "mqtt://iot.yiree.com.cn"  //"mqtt://112.124.38.58" //"mqtt://iot.yiree.com.cn",

static esp_mqtt_client_config_t mqtt_cfg = {
#ifdef MQTT_BROKEY_ADDR_URL
    .broker.address.uri = MQTT_BROKEY_ADDR_URL,
#else
    .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
    .broker.address.hostname = "112.124.38.58",
#endif
    .broker.address.port = 1883,
    .credentials.username = NULL,
    .credentials.client_id = NULL,
    .credentials.set_null_client_id = false,
    .credentials.authentication.password = NULL,
    .buffer.size = MQTT_RX_BUFF_SIZE,
    .buffer.out_size = MQTT_RX_BUFF_SIZE,
    .session.keepalive = 60,
    .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
    .network.disable_auto_reconnect = false,
}; 

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static const uint8_t MQTT_CONNECTED_EVENT     = BIT0;        // MQTT连接成功
static const uint8_t MQTT_DISCONNECTED_EVENT  = BIT1;        // MQTT断开成功
static const uint8_t MQTT_RECONNECT_EVENT     = BIT2;        // MQTT使能重连
static const uint8_t MQTT_PUBLISHED_EVENT     = BIT4;        // MQTT发布成功
static EventGroupHandle_t xEvent = NULL;
static QueueHandle_t xQueue = NULL;     
static mqtt_data_t mqtx = { 0 }; 

static bool mqtt_event_wait(const uint8_t event, uint32_t wait_time)
{
    if (xEvent == NULL) return false;
    EventBits_t uxBits = xEventGroupWaitBits(xEvent, event, pdFALSE, pdFALSE, wait_time / portTICK_PERIOD_MS);
    if (uxBits & event) {  
        return true;
    } 
    return false;
}

// MQTT连接状态
bool mqtt_connect_status(uint32_t wait_time)
{
    return mqtt_event_wait(MQTT_CONNECTED_EVENT, wait_time);
}

// 1: start; 0: stop
static bool esp_mqtt_client_switch(bool status)
{
#define MQTT_START  0X01
#define MQTT_STOP   0X00
    static bool mqtt_switch = false;
    if (status == false && mqtt_switch == true) {
        mqtt_switch = false;
        esp_mqtt_client_stop(mqtt_handle); 
    }
    else if (status == true && mqtt_switch == false) {
        mqtt_switch = true;
        esp_mqtt_client_start(mqtt_handle);
    }
    return mqtt_switch;
}

int app_mqtt_publish_cloud(const char *data, uint16_t len)
{
    if (wifi_connect_status(0) == false) return -1;  // WIFI未连接
    if (mqtt_connect_status(0) == false) return -2;  // MQTT没有连接上
    #if 1 // #ifdef WIFI_MQTT_DEBUG_ENABLE    
    ESP_LOGI(TAG, "app_mqtt_publish_cloud = %s | %d", data, len);
    #endif
    xEventGroupClearBits(xEvent, MQTT_PUBLISHED_EVENT);
    esp_mqtt_client_publish(mqtt_handle, mqtt_topic.cloud, data, len, mqtt_topic.qos, 0);
    return mqtt_event_wait(MQTT_PUBLISHED_EVENT, 300);
}
 
 

//================================================================================================================
//================================================================================================================
static mqtt_callback_t mqtt_subscribe_callback_func = NULL; 

// MQTT订阅接收消息服务回调函数
void mqtt_subscribe_register_callback(mqtt_callback_t callback_func)
{
    mqtt_subscribe_callback_func = callback_func;
}
//================================================================================================================
//================================================================================================================
  
void mqtt_subscribe_task(void * arg)
{
    mqtx.data = heap_caps_malloc(MQTT_RX_BUFF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(mqtx.data);
 
    mqtt_data_t mqrx = { 0 }; 
    mqrx.data = heap_caps_malloc(MQTT_RX_BUFF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(mqrx.data);
 
    while (1) {  // 执行接收任务

        mqtt_event_wait(MQTT_RECONNECT_EVENT, portMAX_DELAY);  // 阻塞等待使能重连

        static bool wifi_status_last = false;
        bool wifi_status = wifi_connect_status(2000);  // 获取WIFI状态
        if (wifi_status_last != wifi_status) {
            wifi_status_last = wifi_status;
            if (wifi_status == true) {
                vTaskDelay(1400);  // WIFI刚连接上；需要延时等待WIFI连接稳定，再启动MQTT
            }
            esp_mqtt_client_switch(wifi_status);  // 根据WIFI连接状态,启停MQTT
        }
 
        if (wifi_status == true && xQueueReceive(xQueue, &mqrx, 2000) == pdTRUE) {  // portMAX_DELAY 接收数据
            #ifdef WIFI_MQTT_DEBUG_ENABLE    
            ESP_LOGI(TAG, "xQueue->data: %s | %d", mqrx.data, mqrx.len);
            #endif
            mqtt_subscribe_callback_func(mqrx);  // APP用户回调函数去执行
        } 
    }
    vTaskDelete(NULL);
}

void app_mqtt_client_disconnect(void)
{
    if (wifi_connect_status(0) == false) return;  // WIFI未连接
    if (xEvent == NULL) return;
    xEventGroupClearBits(xEvent, MQTT_RECONNECT_EVENT);  // 禁止重连！
    if (mqtt_connect_status(0) == true) {         // 如果以前是连接状态！
        esp_mqtt_client_disconnect(mqtt_handle);  // 断开MQTT连接！
        mqtt_event_wait(MQTT_DISCONNECTED_EVENT, 3000); // 等待MQTT断开连接！！！
    }
    esp_mqtt_client_switch(MQTT_STOP);
}
 
//================================================================================================================
//================================================================================================================

bool app_mqtt_client_reconnect(nvs_mqtt_t nvs_mqtt)
{
    mqtt_topic.qos = nvs_mqtt.qos;
    mqtt_cfg.broker.address.port = nvs_mqtt.port;
    // mqtt_cfg在初始化时，就已经指针指向了nvs_mqtt。所以不用重复赋值！
#ifdef WIFI_MQTT_DEBUG_ENABLE    
    printf("-----app_mqtt_client_reconnect-------\r\n");
    ESP_LOGI(TAG, "mqtt_cfg.host: %s", mqtt_cfg.broker.address.hostname);
    ESP_LOGI(TAG, "mqtt_cfg.port: %ld", mqtt_cfg.broker.address.port);
    ESP_LOGI(TAG, "mqtt_cfg.username: %s", mqtt_cfg.credentials.username);
    ESP_LOGI(TAG, "mqtt_cfg.password: %s", mqtt_cfg.credentials.authentication.password);
    printf("\r\n");
#endif

    app_mqtt_client_disconnect();  // 先断开连接
 
    ESP_ERROR_CHECK( esp_mqtt_set_config(mqtt_handle, &mqtt_cfg) ); 
 
    vTaskDelay(1000); // 等待配置完成；同时等待WIFI获取到IP地址，不然会出错!!!

    xEventGroupSetBits(xEvent, MQTT_RECONNECT_EVENT);  // 配置完成，尝试重连MQTT

    #ifdef WIFI_MQTT_DEBUG_ENABLE
    ESP_LOGI(TAG, "[app_mqtt_client_reconnect] esp_mqtt_client_start...\r\n");
    #endif
 
    bool mqtt_status = mqtt_connect_status(8000);
#ifdef WIFI_MQTT_DEBUG_ENABLE    
    if (mqtt_status == true) {  
        ESP_LOGI(TAG, "[app_mqtt_client_reconnect] MQTT Connected OK...");
    } else {  // 连接失败！
        ESP_LOGW(TAG, "[app_mqtt_client_reconnect] MQTT Connected ERR...");
    }  
#endif
    return mqtt_status;  
}

//=========================================================================================
//=========================================================================================
/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t  event  = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:     /*!< The event occurs before connecting */
            break;
        case MQTT_EVENT_CONNECTED: {
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            #endif
            esp_mqtt_client_subscribe(client, mqtt_topic.sntp,  mqtt_topic.qos);
            esp_mqtt_client_subscribe(client, mqtt_topic.local, mqtt_topic.qos);
            xEventGroupSetBits(xEvent, MQTT_CONNECTED_EVENT);
            xEventGroupClearBits(xEvent, MQTT_DISCONNECTED_EVENT);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED,  msg_id=%d", event->msg_id);
            #endif
            if (mqtt_event_wait(MQTT_RECONNECT_EVENT, 0)) {
                esp_mqtt_client_reconnect(client);  
            }
            xEventGroupSetBits(xEvent, MQTT_DISCONNECTED_EVENT);
            xEventGroupClearBits(xEvent, MQTT_CONNECTED_EVENT);
            break;      
        case MQTT_EVENT_SUBSCRIBED:   
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            #endif
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            #endif
            break;
        case MQTT_EVENT_PUBLISHED:  /* 发布消息成功会触发 */
            xEventGroupSetBits(xEvent, MQTT_PUBLISHED_EVENT);
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            #endif
            break;
            
        case MQTT_EVENT_DATA: { /* 订阅消息成功会触发 */
            #ifdef WIFI_MQTT_DEBUG_ENABLE  
            printf("TOPIC = %.*s\r\n", event->topic_len, event->topic);
            printf("DATA = %.*s\r\n", event->data_len, event->data);
            #endif
            if (event->data_len <= 0) break;
            memcpy(mqtx.data, event->data, event->data_len);
            mqtx.len = event->data_len;
            mqtx.data[mqtx.len] = '\0';
            xQueueSend(xQueue, &mqtx, (TickType_t)0);  // 发送队列数据
            break;
        }  
        case MQTT_EVENT_ERROR:
            #ifdef WIFI_MQTT_DEBUG_ENABLE 
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            #endif
            break;
        default:
            #ifdef WIFI_MQTT_DEBUG_ENABLE 
            ESP_LOGI(TAG, "MQTT Other event id:%d", event->event_id);
            #endif
            break;
    }
}
 
void app_mqtt_init(nvs_mqtt_t *nvs_mqtt)
{
    if (nvs_mqtt == NULL)  return;

    esp_log_level_set("MQTT_CLIENT", ESP_LOG_WARN);
 
#if 0  // 测试给一个能联网的MQTT
    if (nvs_mqtt->status == MQTT_CONFIG_NULL) {  // 未配网
        #define APP_CONFIG_MQTT_HOST                "iot.yiree.com.cn"
        #define APP_CONFIG_MQTT_PORT                1883
        #define APP_CONFIG_MQTT_USERNAME            "YiRoot"
        #define APP_CONFIG_MQTT_USERWORD            "b0f7eb1accf6"
        memcpy(nvs_mqtt->host, APP_CONFIG_MQTT_HOST, strlen(APP_CONFIG_MQTT_HOST));
        memcpy(nvs_mqtt->username, APP_CONFIG_MQTT_USERNAME, strlen(APP_CONFIG_MQTT_USERNAME));
        memcpy(nvs_mqtt->userword, APP_CONFIG_MQTT_USERWORD, strlen(APP_CONFIG_MQTT_USERWORD));
        nvs_mqtt->port = APP_CONFIG_MQTT_PORT;
        nvs_mqtt->status = MQTT_CONFIG_OK;
    }
#endif

    uint8_t net_addr[6] = { 0 };
    static char client_id[13];
    esp_read_mac(net_addr, ESP_MAC_BT);     // 读取network addr
    mac_utils_hex2str(net_addr, client_id); // client_id 设置为MAC_ADDR
    mqtt_topic.qos = nvs_mqtt->qos; 
    mqtt_cfg.credentials.client_id = client_id;
    sprintf(mqtt_topic.cloud, "yiree/%s/cloud",  mqtt_cfg.credentials.client_id);   // 发布云端主题
    sprintf(mqtt_topic.local, "yiree/%s/local",  mqtt_cfg.credentials.client_id);   // 订阅设备主题 
    sprintf(mqtt_topic.sntp,  "yiree/sntp/local");   // 获取时间戳的主题！     
    mqtt_cfg.broker.address.port = nvs_mqtt->port;
    mqtt_cfg.credentials.username = nvs_mqtt->username;
    mqtt_cfg.broker.address.hostname = nvs_mqtt->host;
    mqtt_cfg.credentials.authentication.password = nvs_mqtt->userword;
#ifdef WIFI_MQTT_DEBUG_ENABLE 
    printf("\r\n----------------- MQTT ----------------------\r\n");
    ESP_LOGI(TAG, "nvs_mqtt->status = %d", nvs_mqtt->status);
    ESP_LOGI(TAG, "mqtt_cfg.host: %s", mqtt_cfg.broker.address.hostname);
    ESP_LOGI(TAG, "mqtt_cfg.port: %ld", mqtt_cfg.broker.address.port);
    ESP_LOGI(TAG, "mqtt_cfg.username: %s", mqtt_cfg.credentials.username);
    ESP_LOGI(TAG, "mqtt_cfg.password: %s", mqtt_cfg.credentials.authentication.password);
    ESP_LOGI(TAG, "mqtt_cfg.client_id: %s", mqtt_cfg.credentials.client_id);
    ESP_LOGI(TAG, "mqtt_topic.cloud: %s", mqtt_topic.cloud);
    ESP_LOGI(TAG, "mqtt_topic.local: %s", mqtt_topic.local);
    ESP_LOGI(TAG, "mqtt_topic.sntp: %s", mqtt_topic.sntp);
    printf("----------------- MQTT ----------------------\r\n");
#endif
  
    mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    // ESP_ERROR_CHECK( esp_mqtt_client_start(mqtt_handle) );  // 等待wifi连接成功再启动MQTT！ 

    xEvent = xEventGroupCreate();
    xQueue = xQueueCreate(3, sizeof(mqtt_data_t));
    if (nvs_mqtt->status & MQTT_CONFIG_OK) { // 配网过了！
        xEventGroupSetBits(xEvent, MQTT_RECONNECT_EVENT);
    }
    xTaskCreatePinnedToCore(mqtt_subscribe_task, "mqtt_sub_task", 4 * 1024, NULL, 10, NULL, APP_CPU_NUM);       
}

 
 