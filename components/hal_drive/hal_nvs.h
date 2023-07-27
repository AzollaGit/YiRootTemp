/**
 * @file    hal_nvs.h
 * @author  Azolla (1228449928@qq.com)
 * @brief    
 * @version 0.1
 * @date    2022-12-06
 * 
 * @copyright Copyright (c) 2022
 * */

#ifndef __HAL_NVS_H__
#define __HAL_NVS_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "nvs.h"

/**
 * @brief Mode of opening the non-volatile storage
 */
typedef enum {
	NVS_MODE_READ,   /*!< Read only */
	NVS_MODE_WRITE,  /*!< Read and write */
    NVS_MODE_ERASE   /*!< Erase key */
} nvs_mode_t;

esp_err_t nvs_blob_handle(void *value, size_t size, const char *key, nvs_mode_t nvs_mode);

esp_err_t nvs_u8_handle(uint8_t *value, const char *key, nvs_open_mode_t nvs_open_mode);

esp_err_t nvs_u16_handle(uint16_t *value, const char *key, nvs_open_mode_t nvs_open_mode);

#ifdef __cplusplus
} // extern "C"
#endif

#endif  /*__APP_NVS_H__ END.*/
