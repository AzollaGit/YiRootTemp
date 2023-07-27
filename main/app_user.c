/**
 * @file    app_user.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-09-19
 * 
 * @copyright Copyright (c) 2022
 * */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/base64.h"

#include "ble_gatts.h"
#include "ble_gattc.h"
#include "ble_mesh.h"
#include "ble_bind.h"

#include "wifi_init.h"
#include "wifi_sock.h"
#include "wifi_sntp.h"
#include "wifi_user.h"
#include "wifi_ota.h"
#include "wifi_mqtt.h"
 
#include "app_user.h"

#define APP_CONFIG_MQTT_ENABLE    1 // 1: 使能MQTT; 0: TCP/UDP 
#define APP_USER_DEBUG_ENABLE     1

#define TAG  "app_user"

//==============================================================================================================
//==============================================================================================================
static nvs_wifi_t nvs_wifi;
#if APP_CONFIG_MQTT_ENABLE
static nvs_mqtt_t nvs_mqtt;
#endif   

static self_info_t self;
 
static uint8_t audio_voice = 0;   // 音频

static bool sys_reset_enable = 0;    // 1: 软件复位

static hsv_t hsv = { 0 };

static uint8_t led_banlk = 0;  // led闪烁次数

static EventGroupHandle_t xEvent = NULL;  
static SemaphoreHandle_t  xSemap = NULL; 

static char mid_value[14];      // 消息ID字符串值；10位时间戳 + 3位编码
static char mid_ota_value[14];  // OTA时的消息ID值
 
//==============================================================================================================
//==============================================================================================================
static char *clear_handler(app_parse_data_t item);

//==============================================================================================================
//==============================================================================================================
#define KEY_RESET_FACTORY   0       // IO0
#define KEY_CLEAR_NETWORK   21      // IO21
static void key_scan_task(void)
{
    static uint8_t key_time = 0;
    static uint8_t key_value = 0;
    union8_t key_temp = { .value = 0 };
    key_temp.bits.bit0 = !gpio_get_level(KEY_RESET_FACTORY);
    key_temp.bits.bit1 = !gpio_get_level(KEY_CLEAR_NETWORK);
    // ESP_LOGI(TAG, "KEY->key_temp = 0x%02x", key_temp.value);
    if (key_value && key_value == key_temp.value) { // 键值保持不变！
        key_value = key_temp.value;
        if (++key_time > 50) {  // 5s
            if (key_temp.bits.bit0 == true) {  // IO0
                esp_ota_reset_factory();  // 恢复到出厂固件
            } else if (key_temp.bits.bit1 == true) {   // IO21
                app_parse_data_t item;
                clear_handler(item); // 清除网络
            }
            sys_reset_enable = true;  // 使能软复位
        }
    } else { // 键值发现了变化！
        if ((key_value && !key_temp.value) && (key_time > 1 && key_time < 10)) {  // 单击
            #ifdef APP_USER_DEBUG_ENABLE  // debug
            ESP_LOGI(TAG, "KEY->Click = 0x%02x\r\n", key_value);
            #endif 
        }
        key_value = key_temp.value;
        key_time = 0;
    }
    return;
}

//=====================================================================================
//=====================================================================================
bool get_sys_config_network(void)  // LVGL显示用
{
    return nvs_wifi.status == WIFI_CONFIG_NULL ? false : true;  // 是否配网成功了
}

void esp_free_heap_print(void)
{
    #if 1  // debug
    ESP_LOGI(TAG, "Free heap, current: %ld, minimum: %ld", 
                esp_get_free_heap_size(), 
                esp_get_minimum_free_heap_size());  // 打印内存
    ESP_LOGI(TAG, "MALLOC_CAP_SPIRAM = %d, MALLOC_CAP_INTERNAL = %d\n\n", 
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    #endif
}


//=============================================================================================================== 
//=============================================================================================================== 
// 用户逻辑功能任务
static void app_user_task(void *arg)
{ 
    hsv_t hsv_last = { 0 };
    hsv.saturation = 100;  

    union8_t net_curr = { 0 };  // 当前网络状态
    union8_t net_last = { 0 };  // 上次网络状态
    #define NET_BLE_STATE       bits.bit0
    #define NET_WIFI_STATE      bits.bit1
    #define NET_SERVER_STATE    bits.bit2

    #define NET_BLE_ICON        bits.bit4
    #define NET_WIFI_ICON       bits.bit5
    #define NET_SERVER_ICON     bits.bit6
    voice_set_vol(3);  // 设置音量！
    vTaskDelay(500);
    voice_set_vol(3);  // 设置音量！
    vTaskDelay(500);
    if (get_sys_config_network()) {  // 配过成功了！
        audio_voice = VOICE_COMM_WIFI_CONNECTING;  // 提示正在连接网络
    } else {
        audio_voice = VOICE_COMM_POWER_ON;  // 开机声
    }

    while (true) {
        // START...
        //=============================================================================
 
        //=============================================================================
        vTaskDelay(100);  /* 主任务执行的周期/MS */  

        #if 1
        static uint8_t freetime = 0;
        if (++freetime > 150) {
            freetime = 0;
            esp_free_heap_print();
        }
        #endif

        //=============================================================================
        key_scan_task();

        //=============================================================================
        /* UDP定时广播 */
        #if !APP_CONFIG_MQTT_ENABLE
        upd_fixed_time_adv(10);  
        #endif

        //=============================================================================
        /* OTA升级状态 */
        static int ota_plan_last = 0;
        static uint8_t ota_timecnt = 100;
        int ota_plan = wifi_ota_update_plan();
        if ((ota_plan != 0 && ++ota_timecnt >= 20) || ota_plan >= 100) {  // 正在OTA, 定时2S上报一次进度
            if (ota_plan_last != ota_plan) {  // 变化了再上报！
                ota_timecnt = 0;
                if (ota_plan_last == 0)  {  
                    audio_voice = VOICE_COMM_OTA;  // 提示OTA开始
                } else if (ota_plan >= 100) {
                    audio_voice = VOICE_COMM_OTA_OK;
                    sys_reset_enable = true;  // 使能软复位
                } 
                ota_plan_last = ota_plan;
                #ifdef APP_USER_DEBUG_ENABLE  // debug
                ESP_LOGI(TAG, "ota_plan: %d\n", ota_plan);
                #endif
                char ota_str[5];
                itoa(ota_plan, ota_str, 10);
                app_upper_cloud_format(ROOT_OWN_ADDR, mid_ota_value, "ota", (char *)ota_str, 0);  // 发送OTA进度
            }
        }  
 
        //=============================================================================
        if (get_sys_config_network()) {  // 配网成功了！
            //=============================================================================
            /* WIFI连接状态 */
            net_curr.NET_WIFI_STATE = wifi_connect_status(0);
            if (net_last.NET_WIFI_STATE != net_curr.NET_WIFI_STATE) {
                net_last.NET_WIFI_STATE = net_curr.NET_WIFI_STATE;
                if (net_curr.NET_WIFI_STATE == true) {  // WIFI刚连接上
                    audio_voice = VOICE_COMM_WIFI_CONNECT_OK;
                } else {  // WIFI刚断开连接
                    audio_voice = VOICE_COMM_WIFI_DISCONNECT;
                }
            }   
    
            //=============================================================================
            /* 服务器连接状态 */
            #if APP_CONFIG_MQTT_ENABLE
            net_curr.NET_SERVER_STATE = mqtt_connect_status(0);
            #else 
            net_curr.NET_SERVER_STATE = tcp_client_connect_status();
            #endif
            if (net_last.NET_SERVER_STATE != net_curr.NET_SERVER_STATE) {
                net_last.NET_SERVER_STATE = net_curr.NET_SERVER_STATE;
                if (net_curr.NET_SERVER_STATE == true) {   // MQTT连接成功 
                    audio_voice = VOICE_COMM_SERVER_CONNECT_OK;  // 每次连接上MQTT时，都上报一次版本号！
                    vTaskDelay(500);
                    app_upper_cloud_format(ROOT_OWN_ADDR, "0", "ver", (char *)self.version,  0);
                } else { // MQTT刚断开连接
                    audio_voice = VOICE_COMM_SERVER_DISCONNECT;
                } 
            }  
            //=============================================================================
            /* 指示灯 */
            if (net_curr.NET_SERVER_STATE == true) {  // wifi连接上了
                if (net_curr.NET_SERVER_STATE == true) {  // 服务器连接成功
                    if (ota_plan > 0) {  // 在OTA中
                        hsv.value = 10;
                        hsv.hue = hsv.hue < 120? 300 : 60;
                    } else {  
                        hsv.hue = 240;
                    }
                } else {
                    hsv.hue = 120;
                }
            } else {
                hsv.hue = 30;
            }
        } else {  // 还没配网
            hsv.hue = 0;
            //=============================================================================
            /* GATTS 连接状态 */
            net_curr.NET_BLE_STATE = ble_connect_status();
            if (net_last.NET_BLE_STATE != net_curr.NET_BLE_STATE) {
                net_last.NET_BLE_STATE = net_curr.NET_BLE_STATE;
                if (net_curr.NET_BLE_STATE == true) {  // 开始配网
                    audio_voice = VOICE_COMM_WIFI_PAIR;
                } else {  // 配网失败
                    audio_voice = VOICE_COMM_WIFI_PAIR_FAIL; 
                }
            }
        }

        static uint8_t led_cnt = 0;
        if (ota_plan == 0 && hsv.hue < 240 && ++led_cnt >= 8) {
            led_cnt = 0;
            hsv.value = hsv.value ? 0 : 20; // 闪烁
        } else if (led_banlk && ++led_cnt >= 2) {
            led_cnt = 0;
            led_banlk--;
            hsv.value = hsv.value ? 0 : 30;
        }
 
        //=============================================================================
        /* RGB指示灯 */
        if (sys_reset_enable == true) {  // 复位时，亮红灯
            hsv.value = 10;
            hsv.hue = 0;
        }  
        if (memcmp(&hsv_last, &hsv, sizeof(hsv_t))) {  // 有变化！
            memcpy(&hsv_last, &hsv, sizeof(hsv_t));
            hal_rgb_set_hsv(hsv);  // 更新指示灯
        }  
  
        //=============================================================================
        /* 软复位 */
        static uint8_t reset_time = 0;
        if (sys_reset_enable == true) {
            if (++reset_time == 3) { 
                bluedroid_stack_deinit();
                #if APP_CONFIG_MQTT_ENABLE
                app_mqtt_client_disconnect();  // 断开MQTT连接   
                #else
                wifi_sock_close();             // 关闭sock连接
                #endif
                audio_voice = VOICE_COMM_POWER_DOWN;
            } else if (reset_time > 6) { 
                #ifdef APP_USER_DEBUG_ENABLE  // debug
                ESP_LOGI(TAG, "\n\nesp_restart...\n\n");
                #endif
                esp_restart();              // 系统软复位！
            }
        } 

        //=============================================================================
        //=============================================================================
        /* 语音播报！ */
        static uint8_t audio_voice_last = 0x00;
        if (audio_voice_last != audio_voice) {
            audio_voice_last = audio_voice;
            hal_voice_speech(audio_voice); 
        }
 
        //========================================================================================
        // END...
    }
}

//=============================================================================================================== 
//=============================================================================================================== 
 
//===================================================================================
//===================================================================================
//===================================================================================
static char *ver_handler(app_parse_data_t item)
{
    return self.version;
}

// {"addr":"68b6b3341e7a","clear":"all"}
static char *clear_handler(app_parse_data_t item)
{
    if (get_sys_config_network()) {
        nvs_wifi.status = WIFI_CONFIG_NULL;
        audio_voice = VOICE_COMM_SERVER_DISCONNECT;       
    }
    nvs_wifi_reset();          // 清除配网信息！
#if APP_CONFIG_MQTT_ENABLE   
    nvs_mqtt_reset();          // 清除配网信息！
#endif    
    sys_reset_enable = true;   // 使能软复位
    return "ok";
}

static char *ota_handler(app_parse_data_t item)
{
    strcpy(mid_ota_value, mid_value);  // 复制OTA的消息ID
    app_ota_start(item.addr, (char *)item.value);  // 启动OTA
    return NULL;
}
 
static char *sntp_handler(app_parse_data_t item)
{
    // {"addr":"000000000000","sntp":"1680161030"}
    char *value = (char *)item.value;
    value[11] = '\0';
    sntp_sync_time_custom( atoi(value) + 1 ); // 校准时间（+1s网关延时）

    return NULL;
}

// {"bind":"<MAC>|<NAME>"} 
// {"bind":"ok/fail"}    
static char *bind_handler(app_parse_data_t item)
{
    char *value = (char *)item.value;
 
    value[12] = '\0';  // '|' ===>>> '\0'
    uint8_t addr[6];
    mac_utils_str2hex(value, addr); // 设备地址
 
    uint16_t uniaddr = ble_mesh_prov_bind_add(addr, value + 13);  // 添加绑定并更新
    if (uniaddr > 0 ) {
        sprintf(value, "0x%04X", uniaddr); 
        return value; 
    } else {
        return "fail"; 
    }
}

static char *unbind_handler(app_parse_data_t item)
{
    char *value = (char *)item.value;

    if (strcmp(value, "all") == 0) {  // 全部解绑
        ble_mesh_prov_unbind_delete_all();  // 全部解绑
    } else {
        uint16_t uniaddr = strtol(value, NULL, 16);
        uniaddr = ble_mesh_prov_unbind_delete(uniaddr);  // 删除绑定设备
        if (uniaddr == 0) strcpy(value, "fail");
    }
    return value;
}
 
uint8_t app_json_parse(cJSON *jroot);
// 在线情景模式运行
static void scene_run_handler(cJSON *jroot)
{
    cJSON *dtime_item = cJSON_GetObjectItem(jroot, "dtime");
    if (dtime_item != NULL) {
        #ifdef TAG  // debug
        ESP_LOGI(TAG, "dtime = %s", dtime_item->valuestring);
        #endif
    }

    cJSON *action_item = cJSON_GetObjectItem(jroot, "action");
    if (action_item == NULL) {
        #ifdef TAG  // debug
        ESP_LOGE(TAG, "action_item error");
        #endif
        return;
    }
    if (action_item->type != cJSON_Array) return;  // 不是数组类型

    uint8_t jSize = cJSON_GetArraySize(action_item);
    for (uint8_t size = 0; size < jSize; size++) {
        cJSON *arr_item = cJSON_GetArrayItem(action_item, size);  // 遍历
        if (cJSON_IsNull(arr_item))  break; // 为空
        if (arr_item->type != cJSON_Object) continue;
        #ifdef TAG 
        char *data = cJSON_PrintUnformatted(arr_item);
        ESP_LOGI(TAG, "action[%d] = %s", size, data);
        cJSON_free(data);
        #endif
        //===========================================================================
        uint8_t ret = app_json_parse(arr_item);  // 解析JSON数据
        if (ret == APP_PARSE_EXIT || ret == APP_PARSE_NULL) break;
    }
}


typedef struct {
    const char *name;
    char *(*handler)(app_parse_data_t);
} json_handler_t;

static const json_handler_t sys_handler_table[] = {
    /*********** 系 统 指 令 ***********/
    { "ver",        ver_handler         },
    { "sntp",       sntp_handler        },
    { "ota",        ota_handler         },
    { "bind",       bind_handler        },
    { "unbind",     unbind_handler      },
#if SCENE_LOCAL_ENABLE
    /* scene 情景 */
    { "srun",       srun_handler        },
    { "sadd",       sadd_handler        },
    { "sdel",       sdel_handler        },
#endif
    { "clear",      clear_handler       }
}; 

#define SYS_TABLE_SIZE    (sizeof(sys_handler_table) / sizeof(json_handler_t))
//========================================================================================
 
// mesh云端数据处理
static bool mesh_cloud_handler(app_parse_data_t item)
{
    bool ret = false;
    ARRAY_TYPE *value = (ARRAY_TYPE *)item.value;  // 数组格式
    const char *opcode = NULL;

    switch (item.opcode) {
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        ESP_LOGI(TAG, "GEN_ONOFF_SET: ONOFF: %d", value[0]);
        ret = ble_mesh_onoff_set(item.dst_addr, value[0]);
        if (ret) opcode = "0x8204";
        break;
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
        ESP_LOGI(TAG, "GEN_ONOFF_GET: ONOFF");
        ret = ble_mesh_onoff_get(item.dst_addr);
        break;
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET: {
        esp_ble_mesh_state_change_light_hsl_set_t hsl;
        hsl.hue = BIG_DATA_2_OCTET(value, 0);
        hsl.saturation = BIG_DATA_2_OCTET(value, 2);
        hsl.lightness  = BIG_DATA_2_OCTET(value, 4);
        ret = ble_mesh_light_hsl_set(item.dst_addr, hsl);
        if (ret) opcode = "0x8278";
        ESP_LOGI(TAG, "LIGHT_HSL_SET: HSL: %d %d %d", hsl.hue, hsl.saturation, hsl.lightness);
        break;
    }
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET:
        ESP_LOGI(TAG, "LIGHT_HSL_GET");
        ret = ble_mesh_light_hsl_get(item.dst_addr);
        break;
    case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET: {
        esp_ble_mesh_state_change_light_ctl_set_t ctl;
        ctl.lightness = BIG_DATA_2_OCTET(value, 0);
        ctl.temperature = BIG_DATA_2_OCTET(value, 2);
        ctl.delta_uv = 0;
        ret = ble_mesh_light_ctl_set(item.dst_addr, ctl);
        if (ret) opcode = "0x8260";
        ESP_LOGI(TAG, "LIGHT_CTL_SET: lightness %d, temperature %d", ctl.lightness, ctl.temperature); 
        break;
    }
    case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET:
        ESP_LOGI(TAG, "LIGHT_CTL_GET");
        ret = ble_mesh_light_ctl_get(item.dst_addr);
        break;
    case GENIE_MODEL_OP_ATTR_SET:    
    case GENIE_MODEL_OP_ATTR_GET:   
        esp_log_buffer_hex("GENIE_MODEL_OP_ATTR_SET", (ARRAY_TYPE *)item.value, item.size);   
        ret = ble_mesh_send_vendor_message(item.dst_addr, item.opcode, (ARRAY_TYPE *)item.value, item.size); 
        break;
    default:
        break;
    }

    if (ret == true) {
        if (opcode != NULL) {
            ESP_LOGI(TAG, "ack mesh_set_opcode = %s", opcode);
            app_upper_cloud_format(item.dst_addr, mid_value, opcode, item.value, item.size);
        }
    } 

    return ret;
}      

//========================================================================================
//========================================================================================
//========================================================================================
 
// 解析设备指令集
static uint8_t app_parse_handler(app_parse_data_t parse, cJSON *item)
{
    // 系统指令集（注意：mesh设备也支持OTA!）
    if (parse.dst_addr == ROOT_OWN_ADDR) {
        for (uint8_t i = 0; i < SYS_TABLE_SIZE; i++) {  // 遍历键名
            if (strcmp(item->string, sys_handler_table[i].name) == 0) {
                switch (item->type) {   
                case cJSON_String:
                    parse.size  = 0;  // 字符串类似长度为0
                    parse.value = item->valuestring;
                    parse.name  = item->string;
                    #ifdef APP_USER_DEBUG_ENABLE  
                    ESP_LOGI(TAG, "%s_handler: %s", sys_handler_table[i].name, (char *)parse.value);
                    #endif
                    break;
                case cJSON_Array:   
                    parse.size  = cJSON_GetArraySize(item);
                    parse.value = (ARRAY_TYPE *)malloc(parse.size * sizeof(ARRAY_TYPE));
                    for (uint8_t i = 0; i < parse.size; i++) {
                        cJSON *aitem = cJSON_GetArrayItem(item, i);  // 取值
                        ((ARRAY_TYPE *)parse.value)[i] = aitem->valueint;
                    }
                    ESP_LOGI(TAG, "%s_handler: %d", sys_handler_table[i].name, ((ARRAY_TYPE *)parse.value)[0]);
                    break;
                case cJSON_Object:  
                    break;
                default:  // error type
                    break;
                }    
 
                const char *result = sys_handler_table[i].handler(parse);
                if (result != NULL) {  // 有数据就响应！
                    #ifdef APP_USER_DEBUG_ENABLE     
                    ESP_LOGI(TAG, "result = %s", result);
                    #endif
                    app_upper_cloud_format(parse.dst_addr, mid_value, item->string, (char *)result, 0);
                }
                if (item->type == cJSON_Array) {  /** 是数组类型记得释放内存!!! */
                    free(parse.value);  // 记得释放内存
                }
                break;
            }
        }
        return APP_PARSE_ROOT;
    }   
 
    if (item->type != cJSON_Array) return APP_PARSE_NULL;  // 数据类型不对

    parse.size  = cJSON_GetArraySize(item);
    parse.value = (ARRAY_TYPE *)malloc(parse.size * sizeof(ARRAY_TYPE));
    for (uint8_t i = 0; i < parse.size; i++) {
        cJSON *aitem = cJSON_GetArrayItem(item, i);  // 取值
        ((ARRAY_TYPE *)parse.value)[i] = aitem->valueint;
    }
    // ESP_LOGI(TAG, "%s_handler: %d", app_handler_table[i].name, ((ARRAY_TYPE *)parse.value)[0]);
    parse.opcode = strtol(item->string, NULL, 16); // 解析出操作码

    bool ret = mesh_cloud_handler(parse);  // mesh云端数据处理！
    if (ret == false) {  // 错误、超时
        app_upper_cloud_format(parse.dst_addr, mid_value, item->string, (char *)"fail", 0);
    }

    free(parse.value);  // 记得释放内存
 
    return APP_PARSE_NODE;
}

// 解析MQTT服务器下发的JSON数据
uint8_t app_json_parse(cJSON *jroot)
{
    uint8_t ret = 0;
    app_parse_data_t parse = { 0 };

    cJSON *addr_item = cJSON_GetObjectItem(jroot, "addr");
    if (addr_item != NULL) {
        if (addr_item->type == cJSON_String) {  // 单播/组地址
            if (addr_item->valuestring[1] == 'x') {  // "0x0001"
                parse.dst_addr = strtol(addr_item->valuestring, NULL, 16);
            } else {  // 其他地址，都为根节点地址
                parse.dst_addr = ROOT_OWN_ADDR;
            }
        } else if (addr_item->type == cJSON_Number) { 
            parse.dst_addr = addr_item->valueint;
        }
        if (parse.dst_addr > 0) {
            ESP_LOGI(TAG, "parse.dst_addr = 0x%04x", parse.dst_addr);
            cJSON_DeleteItemFromObject(jroot, "addr"); // 删除"addr"节点
        }
    }  
 
    /* ***************************** 功能指令集解析 ************************* */
    for (uint8_t index = 0; (parse.dst_addr > 0 && index < cJSON_GetArraySize(jroot)); index++) {
        cJSON *item = cJSON_GetArrayItem(jroot, index);
        if (cJSON_IsNull(item))  break; // 为空
 
        /********************************************************************/
        if (xSemaphoreTake(xSemap, portMAX_DELAY) == pdTRUE) {  // 等待获取互斥信号量
            ret = app_parse_handler(parse, item); // 解析设备指令集
            xSemaphoreGive(xSemap);  // 释放互斥信号量
            if (ret == APP_PARSE_EXIT || ret == APP_PARSE_NULL)  break;   
        }
    }
    return ret;
}

// 解析云端数据任务
static void app_parse_cloud_task(char *data)
{
    led_banlk = 2;  // 接收到MQTT数据时，指示灯闪烁一次

    cJSON *jroot = cJSON_Parse(data);    
    if (jroot == NULL) {  // ERROR!!!
        #ifdef APP_USER_DEBUG_ENABLE  // debug
        ESP_LOGE(TAG, "Sock cJSON_Parse error");
        #endif
        return;
    }
 
#if APP_CONFIG_MQTT_ENABLE
    cJSON *mid_item = cJSON_GetObjectItem(jroot, "mid");
    if (mid_item != NULL) {  
        strcpy(mid_value, mid_item->valuestring);  // 得到消息ID
        cJSON_DeleteItemFromObject(jroot, "mid");   // 删除"mid"节点
    } else {
        mid_value[0] = '0';
        mid_value[1] = '\0';
    }
#endif
    
#if !SCENE_LOCAL_ENABLE  //  在线情景执行
    cJSON *scene_item = cJSON_GetObjectItem(jroot, "srun");
    if (scene_item != NULL) {
        scene_run_handler(scene_item);  // 执行情景模式
    } else {
        app_json_parse(jroot);  // 解析JSON数据
    }
#endif

    /* Remember to free memory */
    cJSON_Delete(jroot);  
    jroot = NULL;
}
 
#if APP_CONFIG_MQTT_ENABLE
// MQTT 接收任务 {O}
static void mqtt_subscribe_callback(mqtt_data_t info)
{ 
    #ifdef APP_USER_DEBUG_ENABLE  // debug
    // ESP_LOGI(TAG, "Free heap, current: %d, minimum: %d", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());  // 打印内存
    ESP_LOGI(TAG, "mqtt_subscribe_callback->data: %s | %d", info.data, info.len);
    #endif
    app_parse_cloud_task((char *)info.data);  
}
#else  
static void wifi_sock_recv_callback(sock_data_t info)
{
    #ifdef APP_USER_DEBUG_ENABLE     
    ESP_LOGI(TAG, "Socket Received[%d] : %s | %d", info.sock, info.data, info.len); 
    #endif

    app_parse_cloud_task((char *)info.data);  
}
#endif



//=====================================================================================
//=====================================================================================
// BLE 配网
static const char *separator = "|";   // 分隔符

// {"wifi":"<SSID>|<PASSWORD>"} // {"wifi":"yiree-zhulao|yiree2021|YIREF6E5D4C3B2A1"}
// {"wifi":"ok/fail"}    
static char *wifi_handler(char *data)
{
    char *temp = strtok(data, separator);
    for (uint8_t index = 0; temp != NULL; index++) {
        uint8_t len = strlen(temp);
        #ifdef APP_USER_DEBUG_ENABLE     
        ESP_LOGI(TAG, "wifi_temp[%d] = %s | %d", index, temp, len);
        #endif
        switch (index) {
        case 0:
            memcpy(nvs_wifi.ssid, temp, len); 
            nvs_wifi.ssid[len] = '\0';  
            break;
        case 1:
            memcpy(nvs_wifi.password, temp, len);  
            nvs_wifi.password[len] = '\0'; 
            break;   
        default:
            break;
        }
        temp = strtok(NULL, separator);
    }  

    if (wifi_sta_reconnect(nvs_wifi.ssid, nvs_wifi.password) == true) {  // 连接成功了！
        nvs_wifi.status = WIFI_CONFIG_WIFI;         // WIFI配网成功 
        #if !APP_CONFIG_MQTT_ENABLE
        nvs_wifi_handle(&nvs_wifi, NVS_READWRITE);  // 存储NVS
        audio_voice = VOICE_COMM_WIFI_PAIR_OK;  // 配网成功
        xEventGroupSetBits(xEvent, BIT0);   // 配网成功
        #endif
        return "ok";
    } else {
        audio_voice = VOICE_COMM_WIFI_PAIR_FAIL;
        return "fail";
    }
}

// {"mqtt":"<HOST>|<PORT>|<USERNAME>|<USERPASSWORD>|<QOS>"} 
// 比如：{"mqtt":"iot.yiree.com.cn|1883|YiRoot|b0f7eb1accf6|0"}
// {"mqtt":"ok/fail"}
#if APP_CONFIG_MQTT_ENABLE 
static char *mqtt_handler(char *data)
{
    char *temp = strtok(data, separator);
    for (uint8_t index = 0; temp != NULL; index++) {
        uint8_t len = strlen(temp); 
        #ifdef APP_USER_DEBUG_ENABLE     
        ESP_LOGI(TAG, "mqtt_temp[%d] = %s | %d", index, temp, len);
        #endif
        switch (index) {
        case 0:  // <HOST>
            memcpy(nvs_mqtt.host, temp, len); 
            nvs_mqtt.host[len] = '\0';  
            break;
        case 1: // <PORT>
            nvs_mqtt.port = atoi(temp); // 字符串 转 数字
            break;
        case 2: // <USERNAME>   
            memcpy(nvs_mqtt.username, temp, len);  
            nvs_mqtt.username[len] = '\0';  
            break;
        case 3: // <USERPASSWORD>
            memcpy(nvs_mqtt.userword, temp, len);  
            nvs_mqtt.userword[len] = '\0';  
            break;
        case 4:  // <QOS> = atoi(temp); 
            nvs_mqtt.qos = temp[0] - '0';
            break;
        default:
            break;
        }
        temp = strtok(NULL, separator);
    }  
 
    if (app_mqtt_client_reconnect(nvs_mqtt) == true) { // 连接MQTT成功
        nvs_mqtt.status = MQTT_CONFIG_OK;           // MQTT配网成功 
        nvs_mqtt_handle(&nvs_mqtt, NVS_READWRITE);  // 存储NVS
        nvs_wifi_handle(&nvs_wifi, NVS_READWRITE);  // 存储NVS
        #if APP_CONFIG_MQTT_ENABLE
        audio_voice = VOICE_COMM_WIFI_PAIR_OK;  // 配网成功
        xEventGroupSetBits(xEvent, BIT0);   // 配网成功
        #endif
        return "ok";
    } else {  // fail...
        audio_voice = VOICE_COMM_WIFI_PAIR_FAIL;
        return "fail";
    }
}
#endif   
typedef struct {
    const char *name;
    char *(*handler)(char *);
} ble_gatts_handler_t; 
static const ble_gatts_handler_t ble_gatts_handler_table[] = {
    { "wifi",       wifi_handler        },
#if APP_CONFIG_MQTT_ENABLE
    { "mqtt",       mqtt_handler        }
#endif    
}; 

#define BLE_GATTS_TABLE_SIZE    (sizeof(ble_gatts_handler_table) / sizeof(ble_gatts_handler_t))

// BLE_GATTS接收回调函数
void ble_gatts_recv_callback(uint8_t idx, uint8_t *data, uint8_t len)
{
    #ifdef APP_USER_DEBUG_ENABLE     
    ESP_LOGI(TAG, "ble_callback = %s | %d", data, len);
    // esp_log_buffer_hex("ble_rx", data, len);
    #endif

    cJSON *jroot = cJSON_Parse((char *)data);    
    if (jroot == NULL) {  // ERROR!!!
        #ifdef APP_USER_DEBUG_ENABLE  // debug
        ESP_LOGE(TAG, "BLE cJSON_Parse error");
        #endif
        return;
    }

    for (uint8_t i = 0; i < BLE_GATTS_TABLE_SIZE; i++) {
        cJSON *item = cJSON_GetObjectItem(jroot, ble_gatts_handler_table[i].name);
        if (item != NULL) { 
            const char *result = ble_gatts_handler_table[i].handler(item->valuestring);
            if (result != NULL) {  // 有数据就响应！
                cJSON_ReplaceItemInObject(jroot, item->string, cJSON_CreateString(result));
                char *cjson_data = cJSON_PrintUnformatted(jroot);
                #ifdef APP_USER_DEBUG_ENABLE     
                ESP_LOGI(TAG, "gatts_result = %s", cjson_data);
                #endif
                ble_gatts_sendto_app((uint8_t *)cjson_data, strlen(cjson_data));  // 发给BLE
                cJSON_free(cjson_data);
            }
            break; 
        }
    }
 
    /* Remember to free memory */
    cJSON_Delete(jroot);  
    jroot = NULL;
}

//===================================================================================================================
//===================================================================================================================
void ble_mesh_recv_callback(mesh_transfer_t param)
{
    #ifdef APP_USER_DEBUG_ENABLE
    ESP_LOGI(TAG, "<ble_mesh_recv_callback> unicast_addr = 0x%04x, opcode = 0x%06lx", param.unicast_addr, param.opcode);
    #endif
    const char *opcode = NULL;
    uint16_t *data = (uint16_t *)param.data;
 
    switch (param.opcode) {
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS: 
        opcode = "0x8204";
        #ifdef APP_USER_DEBUG_ENABLE
        ESP_LOGI(TAG, "GEN_ONOFF_GET: ONOFF: %d", param.data[0]);
        #endif
        break;
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS:
        opcode = "0x8278";
        #ifdef APP_USER_DEBUG_ENABLE
        ESP_LOGI(TAG, "LIGHT_HSL_GET: HSL: %d %d %d", data[0], data[1], data[2]);
        #endif
        break;
    case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_STATUS:
        opcode = "0x8260"; 
        #ifdef APP_USER_DEBUG_ENABLE
        ESP_LOGI(TAG, "LIGHT_CTL_GET: lightness %d, temperature %d", data[0], data[1]);
        #endif
        break;
    case GENIE_MODEL_OP_ATTR_GET:     // 响应
    case GENIE_MODEL_OP_ATTR_STATUS:  // 接收 
        opcode = "0xD402E5";
        #ifdef APP_USER_DEBUG_ENABLE
        esp_log_buffer_hex("ble_mesh data", param.data, param.len);   
        #endif
        break;
    default:
        break;
    }
    if (opcode == NULL) return;
    app_upper_cloud_format(param.unicast_addr, mid_value, opcode, param.data, param.len);
}

//===================================================================================================================
//===================================================================================================================
void ble_gattc_recv_callback(const uint8_t addr[6], uint8_t *data, uint16_t len)
{
    #ifdef APP_USER_DEBUG_ENABLE
    esp_log_buffer_hex("ble_gattc_recv_callback[hex]", data, len);   
    #endif
} 

void uart_recv_callback(uart_port_t uart_port, uint8_t *data, uint16_t lenght)
{
    // ESP_LOGI(TAG, "uart_callback[%d]: %.*s | %d", uart_port, lenght, data, lenght);
    // esp_log_buffer_hex("uart_read", data, lenght);
    ESP_LOGI(TAG, "uart_callback[%d] len = %d", uart_port, lenght);
    
    if (uart_port == UART_NUM_1) {  // MCU透传串口
        // uint16_t len = 0;
        // while (1) { // 防止多帧数据拼在一起，所以需要分帧解析！
        //     len += app_uart_parse(data + len, lenght - len);    // 解析UART数据
        //     if (len + sizeof(rt_pack_t) > lenght) break;
        //     else vTaskDelay(300);  // 稍微延时一下
        // }
    } else if (uart_port == UART_NUM_0) {  // UART0
        /* user code. */
    } 
}
 
//===================================================================================================================
//===================================================================================================================
#define BLE_ENABLE  1
#if BLE_ENABLE
// BLE初始化任务
void app_ble_init_task(void *arg)
{
    bool is_network = get_sys_config_network();
    xEvent = xEventGroupCreate();
    /***************************** Bluedroid LE ****************************** */
    ESP_ERROR_CHECK( bluedroid_stack_init() );    // 初始化"BLE协议栈"
    
    /* BLE 要在WIFI之前先初始化，不然会报错： coexist: [2202407] Error! Should enable WiFi modem sleep when both WiFi and Bluetooth a */
    if (is_network == false) {  // 说明没有配网
        app_ble_gatts_init();   // GATTS配网 STM32G070R8T6
        ble_gatts_register_callback(ble_gatts_recv_callback);
    } else {
        xEventGroupSetBits(xEvent, BIT0); 
    }
    
    while (true) {
        EventBits_t uxBits = xEventGroupWaitBits(xEvent, BIT0, pdFALSE, pdFALSE, portMAX_DELAY);
        if (uxBits & BIT0) {  // 等待注销GATTS
            #ifdef TAG
            ESP_LOGI(TAG, "ble_gatts_deinit_handle...");
            #endif
            if (is_network == false) {       // 刚开始是没有配网的
                vTaskDelay(500);             // 等待GATTS事情处理完
                ble_gatts_deinit_handle();   // 先注销GATTS
                vTaskDelay(1000);            // 延时等待注销完成
            }
            app_ble_mesh_init();         // 再初始化BLE MESH
            ble_mesh_register_callback(ble_mesh_recv_callback);
            #if 0  // 使能GATTC
            vTaskDelay(500);
            app_ble_gattc_init(); 
            ble_gattc_register_callback(ble_gattc_recv_callback);
            #endif 
            break;
        }
    }

    #ifdef TAG
    ESP_LOGI(TAG, "<app_ble_init_task>, exit...");
    #endif

    vEventGroupDelete(xEvent);

    vTaskDelete(NULL);
}
#endif

void spiffs_print_files(void)
{
#ifdef TAG
    uint8_t i = 0;
    DIR *dir = NULL;  
    struct dirent *entry;  
    if ( (dir = opendir(FAT_MOUNT_POINT)) == NULL){  
        ESP_LOGE(TAG, "opendir failed!");  
        return;  
    }
    while ( (entry = readdir(dir)) != NULL) {  
        // 输出文件或者目录的名称, 文件类型   
        printf("filename[%d] = %s | %d\r\n", i++, entry->d_name, entry->d_type); 
    }  
    closedir(dir);    
#endif       
}
//===================================================================================================================
//===================================================================================================================
void app_user_init(void) 
{
    ESP_LOGI(TAG, "app_user_init...");
    mid_value[0] = '0';
    xSemap = xSemaphoreCreateMutex();      // 创建互斥量
    app_get_self_info(&self);  // 读设备自己的信息 
    nvs_wifi_handle(&nvs_wifi, NVS_READONLY);   // 先读WIFI信息
#if 1       
    hal_gpio_init();    // GPIO输入输出初始化！
    hal_rgb_init();
    hal_uart_init(uart_recv_callback);
    hal_voice_init();
    hal_spiffs_init();      // partitions.csv -> spiffs
    // hal_usb_msc_init();  // partitions.csv -> storage
    spiffs_print_files();
    xTaskCreatePinnedToCore(app_user_task, "app_user", 4 * 1024, NULL, 5, NULL, APP_CPU_NUM);
    vTaskDelay(200);  // 等待硬件外设初始化完成！ 释放一些内存再初始化BLE&WIFI
    return;
#endif    

#if BLE_ENABLE
    /***************************** Bluedroid LE ****************************** */
    xTaskCreatePinnedToCore(app_ble_init_task, "ble_init", 3 * 1024, NULL, 3, NULL, APP_CPU_NUM);
    vTaskDelay(200);  // 等待BLE协议初始化！才能初始化WIFI，不然会报错！
    // return;
#endif  

    /***************************** Wi-Fi *******************************/
    app_wifi_init(nvs_wifi.ssid, nvs_wifi.password);
#if APP_CONFIG_MQTT_ENABLE
    nvs_mqtt_handle(&nvs_mqtt, NVS_READONLY);   
    app_mqtt_init(&nvs_mqtt);  
    mqtt_subscribe_register_callback(mqtt_subscribe_callback);
#else // TCP/UDP
    wifi_sock_init();
    wifi_sock_register_callback(wifi_sock_recv_callback);  
#endif  
    /* END...................................................................................*/
}
/* END...................................................................................*/
// 查看错误码信息： https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/error-codes.html
// http://wiki.tjc1688.com/doku.php 

#if 0
 LVGL 内存池使用PSRAM,并使能TE同步，可以节约内部内存！
 ../components/lvgl/src/misc/lv_mem.c:81
 CONFIG_ENABLE_LCD_TE
#endif
