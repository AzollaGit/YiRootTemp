
/**
 * @file    hal_nvs.c
 * @author  Azolla (1228449928@qq.com)
 * @brief    
 * @version 0.1
 * @date    2022-12-06
 * 
 * @copyright Copyright (c) 2022  
 * */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"


#include "hal_nvs.h"

#define STORAGE_NAMESPACE   "_NVS_"

// NVS_MODE_READ,   /*!< Read only */
// NVS_MODE_WRITE,  /*!< Read and write */
// NVS_MODE_ERASE   /*!< Erase key */
esp_err_t nvs_blob_handle(void *value, size_t size, const char *key, nvs_mode_t nvs_mode)
{
    nvs_handle_t nvs_handle;
    nvs_open_mode_t nvs_open_mode;
    if (nvs_mode > NVS_MODE_WRITE) {
        nvs_open_mode = NVS_READWRITE;
    } else {
        nvs_open_mode = nvs_mode;
    }

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, nvs_open_mode, &nvs_handle); // Open
    if (err != ESP_OK) return err;
 
    if (nvs_mode == NVS_MODE_READ) {
        err = nvs_get_blob(nvs_handle, key, value, &size);  // Read
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto nvs_blob_exit;
    } else {
        if (nvs_mode == NVS_MODE_WRITE) {
            err = nvs_set_blob(nvs_handle, key, value, size);  // Write
            if (err != ESP_OK) goto nvs_blob_exit;
        } else {  // Erase
            err = nvs_erase_key(nvs_handle, key);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto nvs_blob_exit;
        }
        err = nvs_commit(nvs_handle);  // Commit
        if (err != ESP_OK) goto nvs_blob_exit;
    }

nvs_blob_exit:    
    nvs_close(nvs_handle);  // Close
    return ESP_OK;
}
 
// NVS_READONLY,  /*!< Read only */
// NVS_READWRITE  /*!< Read and write */ 
esp_err_t nvs_u8_handle(uint8_t *value, const char *key, nvs_open_mode_t nvs_open_mode)
{
    nvs_handle_t nvs_handle;
 
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, nvs_open_mode, &nvs_handle); // Open
    if (err != ESP_OK) return err;
 
    if (nvs_open_mode == NVS_READONLY) {
        err = nvs_get_u8(nvs_handle, key, value);  // Read
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto nvs_u8_exit;
    } else {
        err = nvs_set_u8(nvs_handle, key, *value); // Write
        if (err != ESP_OK) goto nvs_u8_exit;
 
        err = nvs_commit(nvs_handle);   // Commit
        if (err != ESP_OK) goto nvs_u8_exit;
    }

nvs_u8_exit:    
    nvs_close(nvs_handle);  // Close
    return ESP_OK;
}

// NVS_READONLY,  /*!< Read only */
// NVS_READWRITE  /*!< Read and write */ 
esp_err_t nvs_u16_handle(uint16_t *value, const char *key, nvs_open_mode_t nvs_open_mode)
{
    nvs_handle_t nvs_handle;
 
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, nvs_open_mode, &nvs_handle); // Open
    if (err != ESP_OK) return err;
 
    if (nvs_open_mode == NVS_READONLY) {
        err = nvs_get_u16(nvs_handle, key, value);  // Read
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto nvs_u16_exit;
    } else {
        err = nvs_set_u16(nvs_handle, key, *value); // Write
        if (err != ESP_OK) goto nvs_u16_exit;
 
        err = nvs_commit(nvs_handle);   // Commit
        if (err != ESP_OK) goto nvs_u16_exit;
    }

nvs_u16_exit:    
    nvs_close(nvs_handle);  // Close
    return ESP_OK;
}
 