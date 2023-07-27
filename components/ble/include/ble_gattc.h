/**
 * @file    ble_gattc.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   ble gatt client support multi-connection
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef _BLE_GATTC_H_
#define _BLE_GATTC_H_

#define GATTC_BIND_SIZE     2   // 最多存储多少个设备信息

typedef struct {
    uint8_t addr[6];        /**< device mac addr. */
    char    name[16];       /**< device local name. */
} gattc_bind_t;

typedef struct {
    gattc_bind_t node[GATTC_BIND_SIZE];
    bool update;
}  __attribute__((packed)) gattc_bind_nvs_t;


 
typedef void (*ble_gattc_recv_callback_t)(const uint8_t addr[6], uint8_t *data, uint16_t len);

void ble_gattc_register_callback(ble_gattc_recv_callback_t ble_gattc_recv_cb);

void app_ble_gattc_init(void);
 
void app_ble_gap_close_all(void);
 
bool ble_gattc_all_conn_status(void);

void app_ble_gap_reconnect_all(void);

int app_ble_gattc_write_char(const uint8_t addr[6], const uint8_t *data, uint8_t len);

//======================================================
bool app_bind_add(uint8_t addr[6], char *name);

void app_bind_remove_all(void);

bool app_bind_remove(uint8_t addr[6]);

//======================================================

char *app_bind_find_name(uint8_t addr[6]);

bool app_bind_find_addr(const char *name, uint8_t addr[6]);

bool app_bind_find_addr_select(const char *name, uint8_t select, uint8_t addr[6]);



#endif