/**
 * @file    ble_gattc.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   ble gatt client support multi-connection
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2023
 * */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/timers.h"  
#include "mbedtls/base64.h"
#define BLE_MESH_COCXIST_GATTC_ENABLE   1   // 使能BLE_MESH与GATTC共存
#ifdef BLE_MESH_COCXIST_GATTC_ENABLE
#include "esp_ble_mesh_ble_api.h"
#endif
#include "ble_gatts.h"
#include "ble_gattc.h"
#include "mac_utils.h"


#include "ble_mesh_nvs.h"

#define TAG "BLE_GATTC"

#define BLE_GATTC_DEBUG_ENABLE   1

/* register three profiles, each profile corresponds to one connection,
   which makes it easy to handle each connection event */
#define PROFILE_NUM     2 // GATTC_BIND_SIZE  // GATTC 支持的最大的连接数！
#define BLE_GATT_MTU    64   // BLE-MESH不能使能扫描函数
#define SCAN_THE_TIME   0  // 0: 永久扫描  // 30 the unit of the duration is second;
//======================================================================================
//======================================================================================
/* Declare static functions */
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
//======================================================================================
//====================================================================================== 
/* 是使能 notify 固定的特征UUID，不要尝试去修改他！*/ 
#define INVALID_HANDLE   (uint8_t)0x00    // 不要修改！
static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}
};
//======================================================================================
//======================================================================================
// 读取NVS绑定的设备信息，扫描时就根据这个信息去做过滤！
static uint8_t bind_whitelist_num = 0;   // 需要连接的总数

static bool ble_reconn_disable = false;  // true: 静止重连

//======================================================================================
//======================================================================================
// BLE 通信用的队列传输
typedef struct {
#define EVENT_CONNECT_NONE      0X00
#define EVENT_WRITE_CHAR        0X01   
#define EVENT_READ_CHAR         0X02    
#define EVENT_NOTIFY_CHAR       0X04    
#define EVENT_CONNECT_NEW       0X10
#define EVENT_CONNECT_CLOSE     0X80
    uint8_t  app_id;    // app_id 
    uint8_t  event;     // 事件
    uint8_t  len;       // 数据长度
    uint8_t  *data;     // 数组BUFF
} ble_event_t;  /* BLE连接后发送与接收用得到的参数 */
 
static gattc_bind_nvs_t nvs_bind;  // 读取绑定的设备信息
 
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static const uint8_t BLE_START_SCAN_EVENT = BIT0;   // 开启扫描完成
static const uint8_t BLE_STOP_SCAN_EVENT  = BIT1;   // 暂停扫描完成
#ifndef BLE_MESH_COCXIST_GATTC_ENABLE
static const uint8_t BLE_WHITELIST_EVENT  = BIT2;   // 白名单更新完成
#endif
static const uint8_t BLE_GAP_CLOSE_EVENT  = BIT3;   // 断开连接完成
static const uint8_t BLE_GAP_CONN_EVENT   = BIT4;   // 连接完成事件
static const uint8_t BLE_WRITE_OK_EVENT   = BIT5;   // 写成功事件
static EventGroupHandle_t xEvent = NULL;   
static QueueHandle_t xQueue = NULL;
//======================================================================================
//======================================================================================
// 扫描&连接状态
static bool Isconnecting = false;
static bool conn_service_tab[PROFILE_NUM] = { 0 };  // bit=true: 已连接上服务
//======================================================================================
//======================================================================================
static esp_gattc_char_elem_t  *char_elem_result  = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;
//======================================================================================
//====================================================================================== 
#ifndef BLE_MESH_COCXIST_GATTC_ENABLE
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ONLY_WLST, // BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 160,     // 100ms   /* 0x0004 and 0x4000 in 0.625ms units（2.5ms 到 10.24s） */
    .scan_window            = 80,      // 50ms    /* 0x0004 and 0x4000 in 0.625ms units（2.5ms 到 10.24s） */
    // .scan_interval          = 300,     // 500ms   /* 0x0004 and 0x4000 in 0.625ms units（2.5ms 到 10.24s） */
    // .scan_window            = 100,      // 40ms    /* 0x0004 and 0x4000 in 0.625ms units（2.5ms 到 10.24s） */
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE  // BLE_SCAN_DUPLICATE_ENABLE // BLE_SCAN_DUPLICATE_DISABLE
};
#endif
//======================================================================================
//======================================================================================
struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t service_char_handle;
    uint16_t notify_char_handle;
    uint16_t write_char_handle;
    uint16_t uuid16;                // 用UUID16来简单标识是哪个设备！
    esp_bd_addr_t remote_bda;       // 远程主机的MAC
} gattc_profile_inst_t;

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gattc_profile_tab[PROFILE_NUM] = { 0 };
//======================================================================================
//======================================================================================
// ServerUUID16 去区分设备的类型， 是UUID128的就取前面两个字节组成UUID16
#define UUID16_YR_YIREE     0xFFA0   // 谊瑞设备的UUID16 
#define UUID16_HJ_MOTOR     0xFEE9   // 濠江电机的UUID16 
 
// #define COMPANY_YR          0x5259    // 谊瑞YIREE
// #define COMPANY_HJ          0x4C57    // 濠江电机

// 获取BLE事件组
static bool ble_event_group_wait(const uint8_t event, uint32_t wait_time) 
{
    if (xEvent == NULL) return false;
    EventBits_t uxBits = xEventGroupWaitBits(xEvent, event, pdFALSE, pdFALSE, wait_time);
    if (uxBits & event) {  
        return true;
    }  
    return false;
} 
static void ble_gap_start_scan(void);
static bool ble_gap_stop_scan(uint16_t wait_time);
bool ble_gap_close_whitelist(const uint8_t app_id);
bool ble_gap_whitelist_update(bool add_remove, uint8_t addr[6]);
bool app_ble_whitelist_update(bool add_remove, const uint8_t app_id);
//====================================================================================================================
//====================================================================================================================
//====================================================================================================================
// 获取GATTC是否全部连接了
bool ble_gattc_all_conn_status(void)
{
    uint8_t conn_num = 0;
    for (uint8_t i = 0; i < PROFILE_NUM; i++) {
        if (conn_service_tab[i] == true) {
            conn_num++;
        }
    }
    if (conn_num >= bind_whitelist_num) {
        return true;
    }
    return false;
}
 
uint8_t addr[] = {0x58,0xcf,0x79,0x1a,0x21,0xee};
void ble_bind_device_init(void)
{  
    memcpy(nvs_bind.node[0].addr, addr, 6); 
    strcpy(nvs_bind.node[0].name, "YiChairMassage");   
    ble_gattc_nvs_store(&nvs_bind, sizeof(gattc_bind_nvs_t), NVS_MODE_WRITE); 
    
#if 1  
    bind_whitelist_num = 0;
    ble_gattc_nvs_store(&nvs_bind, sizeof(gattc_bind_nvs_t), NVS_MODE_READ);   // 读取绑定的设备信息
    for (uint8_t i = 0, app_id = 0; i < GATTC_BIND_SIZE; i++) {
        if (nvs_bind.node[i].name[0] != '\0') { // 没配置的设备
            memcpy(gattc_profile_tab[app_id].remote_bda, nvs_bind.node[i].addr, 6);
            bind_whitelist_num = ++app_id;  // 计算有多少个BLE设备需要连接的
        }
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "bind: " ADDRSTR "|%s ", ADDR2STR(nvs_bind.node[i].addr), nvs_bind.node[i].name);
        #endif
    }
    #ifdef BLE_GATTC_DEBUG_ENABLE
    ESP_LOGI(TAG, "bind_whitelist_num = %d/%d", bind_whitelist_num, PROFILE_NUM); 
    #endif
 
#else 
    bind_whitelist_num = 3;
    // "94b555311dee", "574c54c7ad1e", "ac67b20a5b9e"
    mac_str2hex("94b555311dee", gattc_profile_tab[0].remote_bda);   
    mac_str2hex("574c54c7ad1e", gattc_profile_tab[1].remote_bda);   
    mac_str2hex("ac67b20a5b9e", gattc_profile_tab[2].remote_bda);  
#endif
}

// 根据addr[]找到对应的设备名 
char *app_bind_find_name(uint8_t addr[6])
{
    for (uint8_t i = 0; i < GATTC_BIND_SIZE; i++) {
        if (memcmp(nvs_bind.node[i].addr, addr, 6) == 0) { // 找一样的MAC地址
            return nvs_bind.node[i].name;
        }
    }
    return NULL;
}

// 根据name 找到对应的 addr[]
bool app_bind_find_addr(const char *name, uint8_t addr[6])
{
    for (uint8_t i = 0; i < GATTC_BIND_SIZE; i++) {
        if (strncmp(nvs_bind.node[i].name, name, strlen(name)) == 0) {  
            memcpy(addr, nvs_bind.node[i].addr, 6);
            return true;
        }
    }
    return false;
}

// 可选择第几个设备
bool app_bind_find_addr_select(const char *name, uint8_t select, uint8_t addr[6])
{
    for (uint8_t i = 0, sel = 0; i < GATTC_BIND_SIZE; i++) {
        if (strncmp(nvs_bind.node[i].name, name, strlen(name)) == 0) {  
            if (++sel > select) {
                memcpy(addr, nvs_bind.node[i].addr, 6);
                return true;
            }
        }
    }
    return false;
}

// 根据addr[6]找到对应的app_id
static uint8_t app_bind_gap_find_addr(const uint8_t addr[6])
{   
    for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
        if (memcmp(gattc_profile_tab[app_id].remote_bda, addr, 6) == 0) { 
            return app_id;
        }
    } 
    return PROFILE_NUM;
}

// 添加绑定
bool app_bind_add(uint8_t addr[6], char *name)
{
    uint8_t nvs_id = GATTC_BIND_SIZE;
    for (uint8_t i = 0; i < GATTC_BIND_SIZE; i++) {
        if (memcmp(nvs_bind.node[i].addr, addr, 6) == 0) { // 找到重复MAC地址了
            nvs_id = i;  // 更新当前的设备信息，支持重复绑定！
            break;
            // return false;  // 不支持重复绑定！
        } 
        if ((nvs_bind.node[i].addr[0] | nvs_bind.node[i].addr[0]) == 0) {  // 该索引没有数据
            if (nvs_id > i) nvs_id = i;  // 用最小索引
        }
    }

    #ifdef BLE_GATTC_DEBUG_ENABLE  
    ESP_LOGI(TAG, "[bind]nvs_id = %d", nvs_id); 
    #endif
 
    if (nvs_id >= GATTC_BIND_SIZE) {
        return false;  // 没有多余空间了！
    }
  
    /* 修改： 当BLE设备长连接时，进行重复绑定时，设备此时是连接状态就直接返回OK 
        三种情况：
        1、设备未添加到白名单中。
        2、设备添加到了白名单中，但是未连接
        3、设备添加到了白名单中，但是已经连接了
    */
    bool ret = false;
    uint8_t app_id = app_bind_gap_find_addr(addr);  // 添加到白名单了
    if (app_id < PROFILE_NUM) {  // 已经正常连接上了！
        if (conn_service_tab[app_id] == true) {
            #ifdef BLE_GATTC_DEBUG_ENABLE  
            ESP_LOGI(TAG, "[bind][%d] = already bind OK...", app_id); 
            #endif
            return true;
        } else {  // 还没连接上，但是添加了白名单里面！
            app_id |= 0x80;  // 需要进行连接
        }
    } else {  // 还没有添加到白名单
        app_id = 0;
    }
 
    // 更新BLE设备列表
    for (; app_id < PROFILE_NUM; app_id++) {
        if ((gattc_profile_tab[app_id].remote_bda[0] | gattc_profile_tab[app_id].remote_bda[1]) == 0) { // 没配置的设备
            memcpy(gattc_profile_tab[app_id].remote_bda, addr, 6);
            #ifdef BLE_GATTC_DEBUG_ENABLE  
            ESP_LOGI(TAG, "[bind]app_ble_whitelist_update app_id = %d", app_id); 
            #endif
            #ifdef BLE_MESH_COCXIST_GATTC_ENABLE
            ret = app_ble_whitelist_update(true, app_id); // 添加白名单
            if (ret == true) {  // 添加白名单成功!
                app_id |= 0x80;  // 需要进行连接
            } 
            #else
            app_id |= 0x80;  // 需要进行连接
            #endif
            break;
        }
    }  
 
    if (app_id & 0x80) {
        app_id &= 0x7F;
        xEventGroupClearBits(xEvent, BLE_GAP_CONN_EVENT);  // 记得先清标识
        ret = ble_event_group_wait(BLE_GAP_CONN_EVENT, 20000);  // 等待连接完成
        if (ret == true) {
            memcpy(nvs_bind.node[nvs_id].addr, addr, 6); 
            strcpy(nvs_bind.node[nvs_id].name, name);   
            ble_gattc_nvs_store(&nvs_bind, sizeof(gattc_bind_nvs_t), NVS_MODE_WRITE);  
        } else {  // 绑定失败了
            #ifdef BLE_MESH_COCXIST_GATTC_ENABLE
            app_ble_whitelist_update(false, app_id); // 删除白名单; 防止后面在白名单里面又自动添加上去了！
            #endif
        }
    }

    #ifdef BLE_GATTC_DEBUG_ENABLE  
    ESP_LOGI(TAG, "bind_[%d] = %d", app_id, ret); 
    ESP_LOGI(TAG, "bind_addr = "MACSTR"", MAC2STR(addr));   
    ESP_LOGI(TAG, "bind_name = %s", name);
    #endif
 
    return ret;
}

// 绑定全部清空
void app_bind_remove_all(void)
{
    memset(&nvs_bind, 0, sizeof(gattc_bind_nvs_t));
    ble_gattc_nvs_store(&nvs_bind, sizeof(gattc_bind_nvs_t), NVS_MODE_WRITE);  
    app_ble_gap_close_all();  // 断开所有连接
}

bool app_bind_remove(uint8_t addr[6])
{
    bool ret = true;
    for (uint8_t i = 0; i < GATTC_BIND_SIZE; i++) {
        if (memcmp(addr, nvs_bind.node[i].addr, 6) == 0) { // 对比是否已经绑定了
            uint8_t app_id = app_bind_gap_find_addr(addr);
            #ifndef BLE_MESH_COCXIST_GATTC_ENABLE
            if (app_id < PROFILE_NUM) {  // 找到对应的app_id
                ret = app_ble_whitelist_update(false, app_id); // 断开设备，并删除白名单
            } 
            #endif
            if (ret == true) {
                memset(&nvs_bind.node[i], 0, sizeof(gattc_bind_t));
                ble_gattc_nvs_store(&nvs_bind, sizeof(gattc_bind_nvs_t), NVS_MODE_WRITE);  
            }

            #ifdef BLE_GATTC_DEBUG_ENABLE    
            ESP_LOGI(TAG, "unbind_[%d] = %d", app_id, ret); 
            ESP_LOGI(TAG, "unbind_addr = "MACSTR"", MAC2STR(nvs_bind.node[i].addr));   
            ESP_LOGI(TAG, "unbind_name = %s", nvs_bind.node[i].name);
            #endif

            return ret;
        }
    }
 
    return ret;  // 没找到也返回成功
    // return false;
}
 
//====================================================================================================================
//====================================================================================================================
//====================================================================================================================

static bool scan_status = false;  // false: stop scan; true: start scan!
static void ble_gap_start_scan(void)
{
    if (scan_status == false) {
        scan_status = true;
        Isconnecting = false;
        #ifdef BLE_MESH_COCXIST_GATTC_ENABLE
        esp_ble_mesh_ble_scan_param_t param;
        param.duration = 0;
        esp_ble_mesh_start_ble_scanning(&param);
        #else
        esp_ble_gap_start_scanning(SCAN_THE_TIME); // 0: 永久扫描；SCAN_THE_TIME
        #endif
    }
}
 
static bool ble_gap_stop_scan(uint16_t wait_time)
{
    if (scan_status == true) {
        scan_status = false;
        xEventGroupClearBits(xEvent, BLE_STOP_SCAN_EVENT);  // 记得先清标识
        #ifdef BLE_MESH_COCXIST_GATTC_ENABLE
        esp_ble_mesh_stop_ble_scanning();
        #else
        esp_ble_gap_stop_scanning();   // 停止广播
        #endif
        return ble_event_group_wait(BLE_STOP_SCAN_EVENT, wait_time);// 等待暂停完成
    }
    return true;
}
 

// 更新白名单
bool ble_gap_whitelist_update(bool add_remove, uint8_t addr[6])
{
#ifndef BLE_MESH_COCXIST_GATTC_ENABLE    
    xEventGroupClearBits(xEvent, BLE_WHITELIST_EVENT);  // 记得先清标识
    if (add_remove == true) bind_whitelist_num++;   // 白名单++
    else if (bind_whitelist_num) bind_whitelist_num--;  // 白名单--
    esp_ble_gap_update_whitelist(add_remove, addr, BLE_WL_ADDR_TYPE_PUBLIC);  // 更新白名单
    return ble_event_group_wait(BLE_WHITELIST_EVENT, 800); // 等待白名单更新完成
#else 
    return true;
#endif
}

// 断开设备，并删除白名单
bool ble_gap_close_whitelist(const uint8_t app_id)
{
#ifdef BLE_GATTC_DEBUG_ENABLE
    ESP_LOGI(TAG, "ble_gap_close_whitelist[%d]...\n", app_id);
#endif    
    #ifndef BLE_MESH_COCXIST_GATTC_ENABLE
    // 先清白名单, 才能断开连接！
    bool ret = ble_gap_whitelist_update(false, gattc_profile_tab[app_id].remote_bda);
    #else 
    bool ret = true;
    #endif
    // 断开连接
    if (conn_service_tab[app_id] == true) {  // 已经正常连接上了！
        esp_ble_gattc_close(gattc_profile_tab[app_id].gattc_if, gattc_profile_tab[app_id].conn_id);  // 断开连接！
        ble_event_group_wait(BLE_GAP_CLOSE_EVENT, 800);  // 等待断开连接完成
        conn_service_tab[app_id] = false;
    }
    // 清变量标识 
    gattc_profile_tab[app_id].uuid16 = 0;
    gattc_profile_tab[app_id].write_char_handle = 0; 
    memset(gattc_profile_tab[app_id].remote_bda, 0, 6); // 清空地址
    return ret;
}

bool app_ble_whitelist_update(bool add_remove, const uint8_t app_id)
{
    bool ret = false;
    ble_reconn_disable = true;  // true: 静止重连
#ifndef BLE_MESH_COCXIST_GATTC_ENABLE       
    ble_gap_stop_scan(500);     // 一定要先暂停广播，才能配置白名单
#endif    
    if (add_remove == true) {   // 添加白名单
        ret = ble_gap_whitelist_update(true, gattc_profile_tab[app_id].remote_bda); 
    } else {  // 删除白名单
        ret = ble_gap_close_whitelist(app_id);  // 断开设备，并删除白名单
    }
    if (ble_gattc_all_conn_status() == false) {  // 检测是否全部连接了
        ble_gap_start_scan();        // 最后开启扫描
    } 
    ble_reconn_disable = false;  // false: 使能重连
    return ret;
}
 

// 断开全部已经连接的设备！，并且不能再去尝试连接
void app_ble_gap_close_all(void)
{
#ifdef BLE_GATTC_DEBUG_ENABLE    
    ESP_LOGI(TAG, "app_ble_gap_close_all...\n");
#endif    
    ble_reconn_disable = true;  // true: 静止重连
    ble_gap_stop_scan(500);     // 一定要先暂停广播，才能配置白名单
 
    for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
        if ((gattc_profile_tab[app_id].remote_bda[0] | gattc_profile_tab[app_id].remote_bda[1])) {
            ble_gap_close_whitelist(app_id);  // 断开设备，并删除白名单
        }
    }

    bind_whitelist_num = 0;     // 白名单清零
}

// 断开指定已经连接的设备, 然后重新连接！
void app_ble_gap_reconnect_appoint(const uint8_t app_id)
{
    // 清变量标识
    conn_service_tab[app_id] = false; 
    gattc_profile_tab[app_id].uuid16 = 0;
    gattc_profile_tab[app_id].write_char_handle = 0; 

    if (ble_reconn_disable == false) {  // false: 使能重连
        ble_gap_start_scan();     // 开启扫描
    }
}

//==============================================================================================================
static ble_gattc_recv_callback_t ble_gattc_recv_cb_func = NULL; 

// MQTT订阅接收消息服务回调函数
void ble_gattc_register_callback(ble_gattc_recv_callback_t ble_gattc_recv_cb)
{
    ble_gattc_recv_cb_func = ble_gattc_recv_cb;
}
// BLE_GATTC读特征值
uint8_t app_esp_ble_gattc_read_char(const uint8_t addr[6], uint16_t read_char_handle)
{
    for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
        if (memcmp(gattc_profile_tab[app_id].remote_bda, addr, 6) == 0) {  // 根据addr找到对应的app_id
            if (conn_service_tab[app_id] == false) return 0xE1;  // 没有连接！
            esp_ble_gattc_read_char(gattc_profile_tab[app_id].gattc_if,
                                    gattc_profile_tab[app_id].conn_id,
                                    read_char_handle,
                                    ESP_GATT_AUTH_REQ_NONE
                                    );
            return app_id;         
        }
    }
    return 0xE2;   
}
// BLE_GATTC写特征值
int app_ble_gattc_write_char(const uint8_t addr[6], const uint8_t *data, uint8_t len)
{
    static ble_event_t ble_event = { 0 };
    if (ble_event.data == NULL) {
        ble_event.data = heap_caps_malloc(BLE_GATT_MTU, MALLOC_CAP_INTERNAL);
        assert(ble_event.data);
    }
    ble_event.len = 0;
    for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
        if (memcmp(gattc_profile_tab[app_id].remote_bda, addr, 6) == 0) {  // 其他设备地址需全匹配
            ble_event.app_id = app_id;  // 找到对应的app_id
            ble_event.len = len;
            break; 
        }
    }

    if (ble_event.len == 0) return -0x1;  // 没有找到！
    if (conn_service_tab[ble_event.app_id] == false) return -0x2;  // 没有连接！
    
    memcpy(ble_event.data, data, len);
    ble_event.data[len] = '\0';
    ble_event.event = EVENT_WRITE_CHAR; 
    return xQueueSend(xQueue, &ble_event, 200);   // 发送队列消息给任务
}

// BLE GATTC 自定义事件处理任务
// 通过队列发送
void ble_gattc_event_task(void * arg)
{
    static ble_event_t ble_event = { 0 };
    ble_event.data = heap_caps_malloc(BLE_GATT_MTU, MALLOC_CAP_INTERNAL);
    assert(ble_event.data);

    while (true) {
        if (xQueueReceive(xQueue, &ble_event, portMAX_DELAY) == pdTRUE) {  
            // ESP_LOGI(TAG, "ble_event.event = 0x%02x", ble_event.event);
            // printf("ble_event.app_id = %d\n", ble_event.app_id);              
            // ESP_LOGI(TAG, "BLE_xQueueReceive: %s | %d", ble_event.data, ble_event.len);
            // esp_log_buffer_hex("BLE_xQueueReceive[hex]", ble_event.data, ble_event.len);   
            uint8_t app_id = ble_event.app_id;  
            switch (ble_event.event) {
            case EVENT_CONNECT_NEW:  // 新增连接
      
                break;
            case EVENT_CONNECT_CLOSE:  // 连接断开了
                
                break;    
            case EVENT_WRITE_CHAR: {   // 写特征值
                uint16_t write_char_handle = gattc_profile_tab[app_id].write_char_handle;
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGI(TAG, "esp_ble_gattc_write_char[%d]: %s | %d", app_id, ble_event.data, ble_event.len);
                #endif
                xEventGroupClearBits(xEvent, BLE_WRITE_OK_EVENT);  // 记得先清标识
                esp_err_t bt_status = esp_ble_gattc_write_char( gattc_profile_tab[app_id].gattc_if,
                                                                gattc_profile_tab[app_id].conn_id,
                                                                write_char_handle,
                                                                ble_event.len,
                                                                ble_event.data,
                                                                ESP_GATT_WRITE_TYPE_RSP, // ESP_GATT_WRITE_TYPE_NO_RSP, //ESP_GATT_WRITE_TYPE_RSP,
                                                                ESP_GATT_AUTH_REQ_NONE);
                if (bt_status != ESP_OK)  { // BT_STATUS_SUCCESS
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGW(TAG, "esp_ble_gattc_write_char->bt_status = 0x%x", bt_status);
                    #endif
                    break;
                } else {  // 写成功
                    if (ble_event_group_wait(BLE_WRITE_OK_EVENT, 500) == true) { // 等待写完成
                        
                    } 
                }
                break;
            }
            case EVENT_NOTIFY_CHAR: // 接收到BLE数据
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGI(TAG,"EVENT_NOTIFY_CHAR: %s | %d", ble_event.data, ble_event.len);
                esp_log_buffer_hex("EVENT_NOTIFY_CHAR[hex]", ble_event.data, ble_event.len);
                #endif
                break;   
            default:
                break;
            }

            if (ble_event.event == EVENT_NOTIFY_CHAR) {
                ble_gattc_recv_cb_func(gattc_profile_tab[app_id].remote_bda, ble_event.data, ble_event.len);
            }
        }
    }
    vTaskDelete(NULL);
}
 

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *gattc_param = (esp_ble_gattc_cb_param_t *)param;

    static ble_event_t ble_event = { 0 };
    if (ble_event.data == NULL) {
        ble_event.data = heap_caps_malloc(BLE_GATT_MTU, MALLOC_CAP_INTERNAL);
        assert(ble_event.data);
    }
 
    uint8_t app_id = 0;
    for (uint8_t i = 0; i < PROFILE_NUM; i++) {  // 获取app_id
        if (gattc_if == gattc_profile_tab[i].gattc_if) {  // 根据gattc_if，找到对应的app_id
            app_id = gattc_profile_tab[i].app_id;
            break;
        }
    }

    /* 以下为具体的事件处理 */
    switch (event) {
    case ESP_GATTC_REG_EVT: {
        if (gattc_param->reg.status == ESP_GATT_OK && gattc_param->reg.app_id == app_id) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGI(TAG, "-----------------ESP_GATTC_REG_EVT[%d]------------------\n", app_id);
            #endif
            if (gattc_param->reg.app_id == PROFILE_NUM - 1) {  // 最后一个
                #ifndef BLE_MESH_COCXIST_GATTC_ENABLE
                esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params); // 设置扫描参数
                if (scan_ret != ESP_OK) {
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGE(TAG, "set scan params error, error code = %x", scan_ret);
                    #endif
                }  
                #else 
                ble_gap_start_scan();
                #endif
            }
        } 
        break;
    }
    case ESP_GATTC_UNREG_EVT:
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "-----------------ESP_GATTC_UNREG_EVT[%d]------------------\n", app_id);
        #endif
        break;
    /* one device connect successfully, all profiles callback function will get the ESP_GATTC_CONNECT_EVT,
     so must compare the mac address to check which device is connected, so it is a good choice to use ESP_GATTC_OPEN_EVT. */
    case ESP_GATTC_CONNECT_EVT:
        #ifdef BLE_GATTC_DEBUG_ENABLE
        // ESP_LOGI(TAG, "ESP_GATTC_CONNECT_EVT\n");
        #endif
        break;
    case ESP_GATTC_OPEN_EVT:
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "------------ESP_GATTC_OPEN_EVT--------------");
        #endif
        if (gattc_param->open.status != ESP_GATT_OK) {
            // open failed, ignore the first device, connect the second device
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "connect device failed, status 0x%x", gattc_param->open.status);  // ESP_GATT_ERROR  =   0x85
            #endif
            ble_gap_start_scan();
            break;
        }
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, gattc_param->open.conn_id); // 设置MTU大小
        if (mtu_ret) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "config MTU error, error code = %x", mtu_ret);
            #endif
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG,"Config mtu failed");
            #endif
            break;
        }
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        #endif
        /* 选择需要连接的服务UUID */
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL); // NULL 查找全部服务
        // esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote.service_uuid); // 这里选择指定的服务UUID
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {  /*!< When GATT service discovery result is got, the event comes */
        uint16_t service_uuid16 = 0;
        if (gattc_param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128) {
            // esp_log_buffer_hex("ESP_GATTC_SEARCH_RES_EVT->UUID128", gattc_param->search_res.srvc_id.uuid.uuid.uuid128, 16); 
            service_uuid16 = (gattc_param->search_res.srvc_id.uuid.uuid.uuid128[0] << 8) | gattc_param->search_res.srvc_id.uuid.uuid.uuid128[1];
        } else {
            // ESP_LOGI("ESP_GATTC_SEARCH_RES_EVT", "UUID16: %x", gattc_param->search_res.srvc_id.uuid.uuid.uuid16);
            service_uuid16 = gattc_param->search_res.srvc_id.uuid.uuid.uuid16;
        }

        // 在这里做选择用哪个服务！
        if ((service_uuid16 == UUID16_YR_YIREE ) ||    /* 谊瑞的设备 */ 
            (service_uuid16 == UUID16_HJ_MOTOR ) )     /* 濠江的电机 */ 
            { 
            #ifdef BLE_GATTC_DEBUG_ENABLE    
            ESP_LOGI(TAG, "Service found conn_id = %d, UUID16 = 0x%04x", gattc_param->search_res.conn_id, service_uuid16);
            #endif
            // ESP_LOGI(TAG, "ESP_GATTC_SEARCH_RES_EVT: conn_id = %x is primary service %d", gattc_param->search_res.conn_id, gattc_param->search_res.is_primary);
            // ESP_LOGI(TAG, "start handle %d end handle %d current handle value %d", gattc_param->search_res.start_handle, gattc_param->search_res.end_handle, gattc_param->search_res.srvc_id.inst_id);
            conn_service_tab[app_id] = true;  // 有这个服务UUID！
            gattc_profile_tab[app_id].uuid16  = service_uuid16;
            gattc_profile_tab[app_id].conn_id = gattc_param->search_res.conn_id;  // 得到conn_id
            gattc_profile_tab[app_id].service_start_handle = gattc_param->search_res.start_handle;
            gattc_profile_tab[app_id].service_end_handle   = gattc_param->search_res.end_handle;
            xEventGroupSetBits(xEvent, BLE_GAP_CONN_EVENT);  // 连接成功
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:  /*!< When GATT service discovery is completed, the event comes */
        if (gattc_param->search_cmpl.status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "search service failed, error status = %x", gattc_param->search_cmpl.status);
            #endif
            break;
        }
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "ESP_GATTC_SEARCH_CMPL_EVT app_id[%d] = %d", app_id, conn_service_tab[app_id]);
        #endif

        /*  1、判断是否有需要的服务
            2、esp_ble_gattc_get_attr_count() 先获取改服务里面有多少个特征值
            3、esp_ble_gattc_get_all_char()   再获取全部特征值信息，包括 UUID properties 
            4、根据UUID properties 分析出哪个是读写通知的UUID，并分别记录下特征句柄char_handle，此后发送需要用到的！
        */
        if (conn_service_tab[app_id] == true) {  /* 获取到服务了*/                        
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     gattc_param->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC, /*!< Gattc characteristic attribute type in the cache */
                                                                     gattc_profile_tab[app_id].service_start_handle,
                                                                     gattc_profile_tab[app_id].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK) {
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
                #endif
                break;
            }
 
            if (count > 0) {
                char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (char_elem_result == NULL) {
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGE(TAG, "gattc no mem");
                    #endif
                } else {
                    status = esp_ble_gattc_get_all_char(gattc_if,
                                                        gattc_param->search_cmpl.conn_id,
                                                        gattc_profile_tab[app_id].service_start_handle,
                                                        gattc_profile_tab[app_id].service_end_handle,
                                                        char_elem_result,
                                                        &count, 0);
                    if (status != ESP_GATT_OK) {
                        ESP_LOGE(TAG, "esp_ble_gattc_get_char_by_uuid error");
                    }
 
                    for (uint8_t i = 0; i < count; i++) {
                        #if 0 // debug
                        ESP_LOGI(TAG, "char_handle = %d; properties = %x", char_elem_result[i].char_handle, char_elem_result[i].properties);
                        if (char_elem_result[i].uuid.len == ESP_UUID_LEN_128) {
                            esp_log_buffer_hex("char_elem_result->UUID128", char_elem_result[i].uuid.uuid.uuid128, 16); 
                        } else {
                            ESP_LOGI("char_elem_result", "UUID16: %x", char_elem_result[i].uuid.uuid.uuid16);
                        }
                        #endif

                        // 根据权限，把对应的char_handle全部记下来，后面的发送接收要用到
                        if (char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) {  // 是通知的权限
                            gattc_profile_tab[app_id].notify_char_handle = char_elem_result[i].char_handle;
                            esp_ble_gattc_register_for_notify(gattc_if, gattc_profile_tab[app_id].remote_bda, gattc_profile_tab[app_id].notify_char_handle);
                        } else if (char_elem_result[i].properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) {
                            gattc_profile_tab[app_id].write_char_handle = char_elem_result[i].char_handle;  // 默认统一写权限
                        }  
                    }
                    free(char_elem_result); /* free char_elem_result */
                    char_elem_result = NULL;
                }
            }
            /* all devices are connected */
            if ( ble_gattc_all_conn_status() == false) {
                ble_gap_start_scan();  // 开启继续扫描！
            } else {  // 全部连接完全了
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGI(TAG, "\nremote_bt_bind_num[All] = %d.....\n\n\n", bind_whitelist_num);
                #endif
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (gattc_param->reg_for_notify.status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "reg notify failed, error status =%x", gattc_param->reg_for_notify.status);
            break;
            #endif
        }
        // 以下代码不要尝试去做任何修改，使能通知的固定操作！
        #if 1
        /*********************** enable notify ************************/
        uint16_t count = 0;
        uint16_t notify_en = 1;
        esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     gattc_profile_tab[app_id].conn_id,
                                                                     ESP_GATT_DB_DESCRIPTOR,   /*!< Gattc characteristic descriptor attribute type in the cache */
                                                                     gattc_profile_tab[app_id].service_start_handle,
                                                                     gattc_profile_tab[app_id].service_end_handle,
                                                                     gattc_profile_tab[app_id].notify_char_handle,
                                                                     &count);
        if (ret_status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
            #endif
        }
        if (count > 0) {
            descr_elem_result = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (descr_elem_result == NULL) {
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGE(TAG, "malloc error, gattc no mem");
                #endif
            } else {
                ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                     gattc_profile_tab[app_id].conn_id,
                                                                     gattc_param->reg_for_notify.handle,
                                                                     notify_descr_uuid,
                                                                     descr_elem_result,
                                                                     &count);
                if (ret_status != ESP_GATT_OK) {
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGE(TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                    #endif
                }

                /* 使能 ESP_GATT_UUID_CHAR_CLIENT_CONFIG */
                if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                    /* gattc写入特征描述符值 */
                    ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                 gattc_profile_tab[app_id].conn_id,
                                                                 descr_elem_result[0].handle,
                                                                 sizeof(notify_en),
                                                                 (uint8_t *)&notify_en,
                                                                 ESP_GATT_WRITE_TYPE_RSP,
                                                                 ESP_GATT_AUTH_REQ_NONE);
                }

                if (ret_status != ESP_GATT_OK) {
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGE(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT error = %d", ret_status);
                    #endif
                } else {
                    #ifdef BLE_GATTC_DEBUG_ENABLE
                    ESP_LOGI(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT OOOKKK...");
                    #endif
                }
                /* free descr_elem_result */
                free(descr_elem_result);
                descr_elem_result = NULL;
            }
            /*********************** enable notify ************************/
        } else {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT decsr not found");
            #endif
        }
        #endif
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:      /*!< When GATT notification or indication arrives, the event comes  */
    case ESP_GATTC_READ_CHAR_EVT:   /*!< When GATT characteristic is read, the event comes */    
        // 这两个事件，都算为BLE接收
        if (event == ESP_GATTC_NOTIFY_EVT) {
            ble_event.len = gattc_param->notify.value_len;
            memcpy(ble_event.data, gattc_param->notify.value, gattc_param->notify.value_len);
        } else {  // ESP_GATTC_READ_CHAR_EVT:         
            if (gattc_param->read.status != ESP_GATT_OK) {
                #ifdef BLE_GATTC_DEBUG_ENABLE
                ESP_LOGE(TAG, "ESP_GATTC_READ_CHAR_EVT, error status = %x", gattc_param->read.status);
                #endif
                break;
            }
            ble_event.len = gattc_param->read.value_len;
            memcpy(ble_event.data, gattc_param->read.value, gattc_param->read.value_len);
        }
        ble_event.app_id = app_id;
        ble_event.event  = EVENT_NOTIFY_CHAR;   // 接收到数据
        ble_event.data[ble_event.len] = '\0';
        xQueueSend(xQueue, &ble_event, 100);   // 发送队列消息给任务
        break;      
    case ESP_GATTC_WRITE_DESCR_EVT:   /*!< When GATT characteristic descriptor write completes, the event comes */
        if (gattc_param->write.status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "ESP_GATTC_WRITE_DESCR_EVT, error status = %x", gattc_param->write.status);
            #endif
            break;
        }
        // ESP_LOGI(TAG, "write descr success");
        break;
    case ESP_GATTC_WRITE_CHAR_EVT: /*!< When GATT characteristic write operation completes, the event comes */
        if (gattc_param->write.status != ESP_GATT_OK) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "ESP_GATTC_WRITE_DESCR_EVT, error status = %x", gattc_param->write.status);
            #endif
        } else {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGI(TAG, "write char success");
            #endif
            xEventGroupSetBits(xEvent, BLE_WRITE_OK_EVENT); // 写特征值成功
        }
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {  /*!< When "service changed" occurs, the event comes */
        #ifdef BLE_GATTC_DEBUG_ENABLE
        esp_bd_addr_t bda; 
        memcpy(bda, gattc_param->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3], (bda[4] << 8) + bda[5]);
        #endif
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT: // 注意：需要根据p_data->disconnect.remote_bda 来判断是哪个设备断开连接了！
        #if 0
        typedef enum {
            ESP_GATT_CONN_UNKNOWN = 0,                      /*!< Gatt connection unknown */               /* relate to BTA_GATT_CONN_UNKNOWN in bta/bta_gatt_api.h */
            ESP_GATT_CONN_L2C_FAILURE = 1,                  /*!< General L2cap failure  */                /* relate to BTA_GATT_CONN_L2C_FAILURE in bta/bta_gatt_api.h */
            ESP_GATT_CONN_TIMEOUT = 0x08,                   /*!< Connection timeout  */                   /* relate to BTA_GATT_CONN_TIMEOUT in bta/bta_gatt_api.h */
            ESP_GATT_CONN_TERMINATE_PEER_USER = 0x13,       /*!< Connection terminate by peer user  */    /* relate to BTA_GATT_CONN_TERMINATE_PEER_USER in bta/bta_gatt_api.h */
            ESP_GATT_CONN_TERMINATE_LOCAL_HOST = 0x16,      /*!< Connection terminated by local host */    /* relate to BTA_GATT_CONN_TERMINATE_LOCAL_HOST in bta/bta_gatt_api.h */
            ESP_GATT_CONN_FAIL_ESTABLISH = 0x3e,            /*!< Connection fail to establish  */         /* relate to BTA_GATT_CONN_FAIL_ESTABLISH in bta/bta_gatt_api.h */
            ESP_GATT_CONN_LMP_TIMEOUT = 0x22,               /*!< Connection fail for LMP response tout */ /* relate to BTA_GATT_CONN_LMP_TIMEOUT in bta/bta_gatt_api.h */
            ESP_GATT_CONN_CONN_CANCEL = 0x0100,             /*!< L2CAP connection cancelled  */           /* relate to BTA_GATT_CONN_CONN_CANCEL in bta/bta_gatt_api.h */
            ESP_GATT_CONN_NONE = 0x0101                     /*!< No connection to cancel  */              /* relate to BTA_GATT_CONN_NONE in bta/bta_gatt_api.h */
        } esp_gatt_conn_reason_t;
        #endif
        if (memcmp(gattc_param->disconnect.remote_bda, gattc_profile_tab[app_id].remote_bda, 6) == 0) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            esp_log_buffer_hex("disconnect.remote_bda", gattc_param->disconnect.remote_bda, 6);  
            ESP_LOGI(TAG, "ESP_GATTC_DISCONNECT_EVT reason[%d] = 0x%x", app_id, gattc_param->disconnect.reason);  
            #endif
           // ESP_GATT_CONN_CONN_CANCEL; 我测试发现可能是GATTS复位断开连接就会出现这个错误！
            if (gattc_param->disconnect.reason == ESP_GATT_CONN_CONN_CANCEL) {  /*!< L2CAP connection cancelled  */ 
                static uint8_t disconnect_cnt = 0;
                if (++disconnect_cnt >= 3) {  // 超过三次就复位
                    esp_restart();  // 关机直接软复位！  出现这个错误就永远连接不上设备了，只能软件复位了！
                }
            } 
            xEventGroupSetBits(xEvent, BLE_GAP_CLOSE_EVENT); // 断开成功
            app_ble_gap_reconnect_appoint(app_id);  // 断开BLE设备
        }
        break;
    default:
        break;
    }
}
// GATTC开始连接
// uint8_t  addr[6];   /*!< Device address */
// uint8_t  addr_type; /*!< Device address type */
bool ble_gattc_connect_start(uint8_t addr[6], uint8_t addr_type)
{
    if (Isconnecting) {  // 正在连接新设备...
        return false;
    }
 
    uint8_t app_id = app_bind_gap_find_addr(addr); 
    if (app_id >= PROFILE_NUM) {  // 没找到对应的addr
        #ifdef BLE_GATTC_DEBUG_ENABLE  // 扫描到了非白名单设备！
        ESP_LOGW(TAG, "app_bind_gap_find_addr[fail addr]: "MACSTR"", MAC2STR(addr)); 
        #endif
        return false;
    }

    Isconnecting = true;  // 正在连接
    ble_gap_stop_scan(0); // 停止扫描, 一定要先暂停扫描，才能执行esp_ble_gattc_open(), 不然连接不上去设备！BUG!!!
    esp_ble_gattc_open(gattc_profile_tab[app_id].gattc_if, addr, addr_type, true);
    
    #ifdef BLE_GATTC_DEBUG_ENABLE
    ESP_LOGW(TAG, "<esp_ble_gattc_open>, addr: "MACSTR"", MAC2STR(addr)); 
    #endif
    return true;
}
 
#ifndef BLE_MESH_COCXIST_GATTC_ENABLE 
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    #ifdef BLE_GATTC_DEBUG_ENABLE
    ESP_LOGI(TAG, "<esp_gap_cb>, event = %d", event);
    #endif
    switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                param->update_conn_params.status,
                param->update_conn_params.min_int,
                param->update_conn_params.max_int,
                param->update_conn_params.conn_int,
                param->update_conn_params.latency,
                param->update_conn_params.timeout);
        #endif        
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {  /*!< When scan parameters set complete, the event comes */
        #ifdef BLE_GATTC_DEBUG_ENABLE
        ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: %d", param->scan_param_cmpl.status);
        #endif
        if (param->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS) break;
        if (bind_whitelist_num == 0) break;   // 没有绑定设备！
        for (uint8_t app_id = 0; app_id < bind_whitelist_num; app_id++) {  // 必须先设置成功扫描参数
            esp_ble_gap_update_whitelist(true, gattc_profile_tab[app_id].remote_bda, BLE_WL_ADDR_TYPE_PUBLIC);  // 添加白名单
        }
        ble_gap_start_scan(); // the unit of the duration is second
        break;
    }
    case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT: {
        #ifdef BLE_GATTC_DEBUG_ENABLE
        if (param->update_whitelist_cmpl.wl_operation == ESP_BLE_WHITELIST_ADD) { /*!< add address to whitelist */
            ESP_LOGI(TAG, "whitelist_udpate ESP_BLE_WHITELIST_ADD");
        } else {
            ESP_LOGI(TAG, "whitelist_udpate ESP_BLE_WHITELIST_REMOVE");
        }
        if (param->update_whitelist_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGI(TAG, "whitelist_udpate success");
            #endif
        } else {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "whitelist_udpate failed = %d", param->update_whitelist_cmpl.status);
            #endif
        }
        uint16_t size;
        esp_ble_gap_get_whitelist_size(&size);
        ESP_LOGI(TAG, "whitelist_udpate size = %d", size);
        #endif
        xEventGroupSetBits(xEvent, BLE_WHITELIST_EVENT); // 白名单更新成功
        break;    
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        // scan start complete event to indicate scan start successfully or failed
        #ifdef BLE_GATTC_DEBUG_ENABLE
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan start success");
        } else {
            ESP_LOGE(TAG, "Scan start failed");
        }
        #endif
        xEventGroupSetBits(xEvent, BLE_START_SCAN_EVENT); // 扫描开始成功
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        #ifdef BLE_GATTC_DEBUG_ENABLE
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Scan stop failed");
            break;
        }
        ESP_LOGI(TAG, "Stop scan successfully");
        #endif
        xEventGroupSetBits(xEvent, BLE_STOP_SCAN_EVENT); // 扫描暂停成功
        break;    
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        // printf("\n\nscan_result->ble_adv:");
        // for (size_t i = 0; i < param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len; i++) {
        //     printf("%02x ", param->scan_rst.ble_adv[i]);
        // } printf(" | %d \n\n", param->scan_rst.search_evt);
        switch (param->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT: {
            esp_log_buffer_hex("scan_rst.bda", param->scan_rst.bda, 6);  // 获取设备MAC地址
            ble_gattc_connect_start(param->scan_rst.bda, param->scan_rst.ble_addr_type);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}
#endif
//====================================================================================
//====================================================================================
#ifdef BLE_MESH_COCXIST_GATTC_ENABLE
static void esp_ble_mesh_ble_cb(esp_ble_mesh_ble_cb_event_t event, esp_ble_mesh_ble_cb_param_t *param)
{
    
    switch (event) {
    case ESP_BLE_MESH_START_BLE_ADVERTISING_COMP_EVT: /*!< Start BLE advertising completion event */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_START_BLE_ADVERTISING_COMP_EVT");
        #endif
        break;
    case ESP_BLE_MESH_STOP_BLE_ADVERTISING_COMP_EVT:  /*!< Stop BLE advertising completion event */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_STOP_BLE_ADVERTISING_COMP_EVT");
        #endif
        break;
    case ESP_BLE_MESH_START_BLE_SCANNING_COMP_EVT:    /*!< Start BLE scanning completion event */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_START_BLE_SCANNING_COMP_EVT");
        #endif
        xEventGroupSetBits(xEvent, BLE_START_SCAN_EVENT);  
        break;
    case ESP_BLE_MESH_STOP_BLE_SCANNING_COMP_EVT:     /*!< Stop BLE scanning completion event */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_STOP_BLE_SCANNING_COMP_EVT");
        #endif
        xEventGroupSetBits(xEvent, BLE_STOP_SCAN_EVENT);  
        break;
    case ESP_BLE_MESH_SCAN_BLE_ADVERTISING_PKT_EVT:   /*!< Scanning BLE advertising packets event */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_SCAN_BLE_ADVERTISING_PKT_EVT-> addr="ADDRSTR", rssi = %d, length = %d", 
                       ADDR2STR(param->scan_ble_adv_pkt.addr),
                       param->scan_ble_adv_pkt.rssi, param->scan_ble_adv_pkt.length);
        #endif   // ESP_BLE_MESH_PROXY_CLIENT_RECV_ADV_PKT_EVT
        ble_gattc_connect_start(param->scan_ble_adv_pkt.addr, param->scan_ble_adv_pkt.addr_type);
        // struct {
        //     uint8_t  addr[6];   /*!< Device address */
        //     uint8_t  addr_type; /*!< Device address type */
        //     uint8_t  adv_type;  /*!< Advertising data type */
        //     uint8_t *data;      /*!< Advertising data */
        //     uint16_t length;    /*!< Advertising data length */
        //     int8_t   rssi;      /*!< RSSI of the advertising packet */
        // } scan_ble_adv_pkt;   
        break;
    default:
        #ifdef TAG
        ESP_LOGW(TAG, "<esp_ble_mesh_ble_cb>, default = %d", event);
        #endif
        break;
    }  
}
#endif

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    // ESP_LOGI(TAG, "EVT %d, gattc_if %d, app_id %d", event, gattc_if, param->reg.app_id);
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gattc_profile_tab[param->reg.app_id].gattc_if = gattc_if;
            gattc_profile_tab[param->reg.app_id].app_id   = param->reg.app_id;
            ESP_LOGI(TAG, "ESP_GATTC_REG_EVT: gattc_if %d, app_id %d", gattc_if, param->reg.app_id);
        } else {
            #ifdef BLE_GATTC_DEBUG_ENABLE
            ESP_LOGE(TAG, "Reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            #endif
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
            if ( gattc_if == gattc_profile_tab[app_id].gattc_if) {
                gattc_profile_tab[app_id].gattc_cb(event, gattc_if, param);
                break;
            }
        }
    } while (0);
}

void app_ble_gattc_init(void)
{
    // esp_log_level_set(TAG, ESP_LOG_WARN);
    // esp_log_level_set("BT_INIT", ESP_LOG_WARN);
    // esp_log_level_set("BLE_INIT", ESP_LOG_WARN);

    xEvent = xEventGroupCreate();
    xQueue = xQueueCreate(2, sizeof(ble_event_t));
 
    ble_bind_device_init();

    // ESP_ERROR_CHECK( bluedroid_stack_init() );   // 初始化BLE协议栈
#ifdef BLE_MESH_COCXIST_GATTC_ENABLE
    esp_ble_mesh_register_ble_callback(esp_ble_mesh_ble_cb); 
#else 
    //register the callback function to the gap module
    ESP_ERROR_CHECK( esp_ble_gap_register_callback(esp_gap_cb) );
#endif 
    // register the callback function to the gattc module
    ESP_ERROR_CHECK( esp_ble_gattc_register_callback(esp_gattc_cb) );
 
    /* 注册多个从机APP */
    for (uint8_t app_id = 0; app_id < PROFILE_NUM; app_id++) {
        gattc_profile_tab[app_id].gattc_cb = gattc_profile_event_handler;
        gattc_profile_tab[app_id].gattc_if = ESP_GATT_IF_NONE;  /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        ESP_ERROR_CHECK( esp_ble_gattc_app_register(app_id) );  // 注册GATTC_APP; app_id 从0开始+1
    }

    xTaskCreatePinnedToCore(ble_gattc_event_task, "ble_gattc_task", 3072, NULL, 8, NULL, PRO_CPU_NUM);
    return;
}
// void gattc_receive_callback(const uint8_t addr[6], uint8_t *data, uint16_t len)
// {

// } 
// app_ble_gattc_init();  // 初始化
// gattc_register_callback(gattc_receive_callback);