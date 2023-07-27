/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef _BLE_MESH_NVS_H_
#define _BLE_MESH_NVS_H_

#include <stdint.h>
#include "esp_err.h"
#include "nvs_flash.h"

#include "hal_nvs.h"

typedef struct  {
   uint16_t server_addr;   /* Vendor server unicast address */
   uint16_t vnd_tid;       /* TID contained in the vendor message */
   uint16_t net_idx;
   uint16_t app_idx;
} mesh_nvs_store_t;

// #define  CONFIG_BLE_MESH_MAX_PROV_NODES
 
esp_err_t ble_mesh_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode);

esp_err_t ble_bind_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode);

esp_err_t ble_gattc_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode);
 
#endif /* _BLE_MESH_NVS_H_ */
