/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
 
#include "ble_mesh_nvs.h"
 
// NVS_MODE_READ,   /*!< Read only */
// NVS_MODE_WRITE,  /*!< Read and write */
// NVS_MODE_ERASE   /*!< Erase key */
esp_err_t ble_mesh_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode)
{
    return nvs_blob_handle(value, size, "_MESH_KEY_", nvs_open_mode);
}

esp_err_t ble_bind_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode)
{
    return nvs_blob_handle(value, size, "_BIND_KEY_", nvs_open_mode);
}

esp_err_t ble_gattc_nvs_store(void *value, size_t size, nvs_mode_t nvs_open_mode)
{
    return nvs_blob_handle(value, size, "_GATTC_KEY_", nvs_open_mode);
}
 