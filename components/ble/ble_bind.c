/**
 * @file    ble_bind.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   ble mesh绑定的设备信息
 * @version 0.1
 * @date    2023-06-14
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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
 

#include "ble_bind.h"

// #define TAG  "ble_bind"
 
static mesh_bind_nvs_t nvs_bind;
 
void mesh_bind_init(void)
{  
    ble_bind_nvs_store(&nvs_bind, sizeof(mesh_bind_nvs_t), NVS_MODE_READ);  

#ifdef TAG    
    for (uint8_t i = 0; i < BIND_TABLE_SIZE; i++) {
        if (nvs_bind.node[i].uniaddr > 0) {
            ESP_LOGI(TAG, "[%d]bind_addr = "MACSTR"|%s, uniaddr = 0x%04x, pid = 0x%02x", i, MAC2STR(nvs_bind.node[i].addr), nvs_bind.node[i].name, nvs_bind.node[i].uniaddr, nvs_bind.node[i].pid);   
        }
    }
#endif  
}

// 根据addr[]找到对应的设备名 
char *mesh_bind_find_name(uint8_t addr[6])
{
    for (uint8_t i = 0; i < BIND_TABLE_SIZE; i++) {
        if (memcmp(nvs_bind.node[i].addr, addr, 6) == 0) { // 找一样的MAC地址
            return nvs_bind.node[i].name;
        }
    }
    return NULL;
}

// 根据name 找到对应的 addr[]
bool mesh_bind_find_addr(const char *name, uint8_t addr[6])
{
    for (uint8_t i = 0; i < BIND_TABLE_SIZE; i++) {
        if (strncmp(nvs_bind.node[i].name, name, strlen(name)) == 0) {  
            memcpy(addr, nvs_bind.node[i].addr, 6);
            return true;
        }
    }
    return false;
}

// 可选择第几个设备
bool mesh_bind_find_addr_select(const char *name, uint8_t select, uint8_t addr[6])
{
    for (uint8_t i = 0, sel = 0; i < BIND_TABLE_SIZE; i++) {
        if (strncmp(nvs_bind.node[i].name, name, strlen(name)) == 0) {  
            if (++sel > select) {
                memcpy(addr, nvs_bind.node[i].addr, 6);
                return true;
            }
        }
    }
    return false;
}

// 添加绑定
bool mesh_bind_add(mesh_bind_t bind)
{
    uint8_t nvs_id = BIND_TABLE_SIZE;
    for (uint8_t i = 0; i < BIND_TABLE_SIZE; i++) {
        if ((nvs_bind.node[i].addr[0] | nvs_bind.node[i].addr[3]) == 0) {  // 该索引没有数据
            if (nvs_id > i) nvs_id = i;  // 用最小索引
        }
        if (memcmp(nvs_bind.node[i].addr, bind.addr, 6) == 0) { // 找到重复MAC地址了
            nvs_id = i;  // 更新当前的设备信息，支持重复绑定！
            break;  // 支持重复绑定！
            // return false;  // 不支持重复绑定！
        } 
    }
 
    if (nvs_id >= BIND_TABLE_SIZE) {
        return false;  // 没有多余空间了！
    }
 
    memcpy(&nvs_bind.node[nvs_id], &bind, sizeof(mesh_bind_t)); 
    nvs_bind.update = true;   // 绑定更新！
 
    #ifdef TAG  
    ESP_LOGI(TAG, "[%d]bind_addr = "MACSTR"|%s, uniaddr = 0x%04x, pid = 0x%02x", nvs_id, MAC2STR(nvs_bind.node[nvs_id].addr), nvs_bind.node[nvs_id].name, nvs_bind.node[nvs_id].uniaddr, nvs_bind.node[nvs_id].pid);   
    #endif
 
    return true;
}

void mesh_bind_update(void)
{
    if (nvs_bind.update == true) { // true: 绑定信息有更新
        vTaskDelay(1000);
        nvs_bind.update = false; 
        ble_bind_nvs_store(&nvs_bind, sizeof(mesh_bind_nvs_t), NVS_MODE_WRITE);       // 更新绑定的设备信息 
    }
}

// 绑定全部清空
void mesh_bind_remove_all(void)
{
    memset(&nvs_bind, 0, sizeof(mesh_bind_nvs_t));
    ble_bind_nvs_store(&nvs_bind, sizeof(mesh_bind_nvs_t), NVS_MODE_ERASE);    
}

bool mesh_bind_remove(uint16_t unicast_addr)
{
    for (uint8_t i = 0; i < BIND_TABLE_SIZE; i++) {
        if (nvs_bind.node[i].uniaddr == unicast_addr) { // 对比是否已经绑定了
            memset(&nvs_bind.node[i], 0, sizeof(mesh_bind_nvs_t));
            nvs_bind.update = true;   // 绑定更新！ 
            #ifdef TAG    
            // ESP_LOGI(TAG, "[%d]unbind_addr = "MACSTR"|%s, uniaddr = 0x%04x, pid = 0x%02x", i, MAC2STR(nvs_bind.node[i].addr), nvs_bind.node[i].name, nvs_bind.node[i].uniaddr, nvs_bind.node[i].pid);   
            #endif
            break;
        }
    }
    return true;  // 没找到也返回成功
}
 