
/**
 * @file    ble_bind.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   ble bind ble-mesh device info.
 * @version 0.1
 * @date    2023-06-14
 * 
 * @copyright Copyright (c) 2023
 * */
#ifndef _BLE_BIND_H_
#define _BLE_BIND_H_

#include "ble_mesh_nvs.h"

/**< device product id. */
#define ROOT_PID      0x00    // 网关
#define BED_PID       0x01    // 智能床
#define LIGHT_PID     0x02    // 灯光
#define SWITCH_PID    0x03    // 开关
#define CURTION_PID   0x04    // 窗帘
#define SUMANBO_PID   0x05    // 舒曼波
#define TUMBLE_PID    0x06    // 跌倒
#define SMR_PID       0x07    // Sleep monitoring radar 睡眠监测雷达

#define DEV_PID       ROOT_PID 

typedef struct {
    uint16_t uniaddr;        /**< device unicast addr. */
    uint8_t  pid;            /**< device product id. */
    uint8_t  addr[6];        /**< device mac addr. */
    char     name[16];       /**< device local name. */
} mesh_bind_t;  // 设备绑定的基本信息

#define BIND_TABLE_SIZE     20   // 最多存储多少个设备信息

typedef struct {
    mesh_bind_t node[BIND_TABLE_SIZE];  
    bool update;
} __attribute__((packed)) mesh_bind_nvs_t;  

//======================================================
void mesh_bind_init(void);

bool mesh_bind_add(mesh_bind_t bind);

bool mesh_bind_remove(uint16_t unicast_addr);

void mesh_bind_update(void);

void mesh_bind_remove_all(void);
//======================================================

char *mesh_bind_find_name(uint8_t addr[6]);

bool mesh_bind_find_addr(const char *name, uint8_t addr[6]);

bool mesh_bind_find_addr_select(const char *name, uint8_t select, uint8_t addr[6]);


#endif /* _BLE_BIND_H_ end. */