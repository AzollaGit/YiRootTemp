/**
 * @file    wifi_ota.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __WIFI_OTA_H__
#define __WIFI_OTA_H__

#include "esp_system.h"
 
typedef struct {
    uint8_t     addr[6];        // MAC地址
    char        bin_name[16];   // 固件名
    char        dev_name[16];   // 设备名
    char        version[12];    // 固件版本
} self_info_t; /* 自己的信息 */

typedef union {
	uint8_t  u8[8];
	uint16_t u16[4];  
	uint32_t u32[2];  
	uint64_t u64;  
} chip_id_t;

/**
 * @brief Firmware packet
 */
typedef struct {
#define OTA_PACKET_SIZE     1024  /**< OTA packet max size */
    uint16_t size;  /**< Size */
    uint8_t  data[OTA_PACKET_SIZE]; /**< Firmware */
}  __attribute__((packed)) ota_packet_t;
 
void app_get_self_info(self_info_t *self);
 
void app_ota_start(uint8_t addr[6], const char *ota_url);
 
esp_err_t esp_ota_reset_factory(void);

int wifi_ota_update_plan(void);

uint16_t esp_read_chip_id(void);

uint8_t esp_read_chip_name(const char *dev_name, char *name);

uint8_t *app_get_ota_addr(void);

#endif  /* __WIFI_OTA_H__ END. */
