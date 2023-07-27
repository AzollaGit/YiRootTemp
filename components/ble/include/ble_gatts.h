/**
 * @file    ble_gatts.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   ble gatt server. WiFi Network Configuration.
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */

#ifndef _BLE_GATTS_H_
#define _BLE_GATTS_H_

#ifdef __cplusplus
extern "C" {
#endif /**< __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "sdkconfig.h"

/*
 * DEFINES
 ****************************************************************************************
 */
#ifndef ADDRSTR
    #define ADDRSTR      "%02x%02x%02x%02x%02x%02x"
    #define ADDR2STR(a)  (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif
 
/* Attributes State Machine */
enum
{
    HRS_IDX_SVC = 0,

    IDX_WRITE_CHAR,
    IDX_WRITE_VAL,

    IDX_NOTIFY_CHAR,
    IDX_NOTIFY_VAL,
    IDX_NOTIFY_CFG,

    HRS_IDX_NB,
};

typedef union {
    uint8_t  byte[4];           // 字节
    uint16_t half_word[2];      // 半字
    uint32_t word;              // 字
} word_union_t;

typedef union {
    uint8_t  uint8[4];            
    uint16_t uint16[2];           
    uint32_t uint32;              
} union32_t;

/*
 * AliGenie GATTS OTA protocol command defines
 ****************************************************************************************
 */
 
#define DEV_FIRMWARE_VERSION            0x00020000                // 设备固件版本号："0001.00.00"  
#define AES_BLANK_SIZE                  16                        // ase128 blank size
//=======================================================================================




//=======================================================================================

// AIS服务声明为Primary Service，Service UUID为0xFEB3。
#define GATTS_CHAR_UUID_PRIMARY         0xFFA0
// AIS（Alibaba IoT Service）服务包含以下5个Characteristics
#define GATTS_CHAR_UUID_WRITE           0xFFA1
#define GATTS_CHAR_UUID_NOTIFY          0xFFA2

#define GATTS_MTU_SIZE                  (128)   // (240) 
#define GATTS_WRITE_DATA_LEN            GATTS_MTU_SIZE
#define GATTS_READ_DATA_LEN             GATTS_MTU_SIZE
#define AES_DATA_SIZE                   GATTS_MTU_SIZE

typedef struct {
    uint8_t value[GATTS_MTU_SIZE];    // tx data
    uint8_t len;
    uint8_t idx;
} ble_transfer_t; 

/*
 * function declaration
 ****************************************************************************************
 */

typedef void (*ble_gatts_recv_callback_t)(uint8_t idx, uint8_t *data, uint8_t len);
void ble_gatts_register_callback(ble_gatts_recv_callback_t ble_gatts_recv_cb);

void app_ble_gatts_init(void);

uint8_t app_read_chip_name(char *name);

void printf_hex_buff(const char *name, const uint8_t *buff, uint16_t len);
void ble_key_update(const uint8_t random[16]);
uint16_t aes128_cbc_pkcs7(const uint8_t secret_key[16], bool mode, uint8_t *input, uint16_t length);

void little_endian_swap_buff(uint8_t *dst, const uint8_t *src, int len);
 
void ble_gatts_sendto_app(uint8_t *data, uint16_t len);
 
uint8_t ble_connect_status(void);

esp_err_t bluedroid_stack_init(void);

void bluedroid_stack_deinit(void);

void ble_gatts_deinit_handle(void); 

#ifdef __cplusplus
}
#endif /**< __cplusplus */

#endif /* _GENIE_GATTS_H_ */