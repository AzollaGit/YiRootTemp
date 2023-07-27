 
/**
 * @file    hal_rs485.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   led contorl PWM
 * @version 0.1
 * @date    2022-12-06
 * 
 * @copyright Copyright (c) 2022
 * */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "sdkconfig.h"
 
#include "hal_uart.h"
#include "hal_rs485.h"

#define TAG "hal_rs485"

#define RS485_UART_PORT   UART_NUM_1

uint8_t rs485_check_xor(uint8_t *data, uint8_t len)
{
    uint8_t check = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        check ^= data[i];
    }
    return check;
}

void hal_rs485_write(uint8_t addr, const uint8_t *data, uint8_t len)
{
    rs485_format_t write;
    write.cmd  = RS485_CMD_WRITE; 
    write.addr = addr;
    write.len  = len;
    memcpy(write.data, data, write.len);
    write.data[len] = rs485_check_xor(write.frame, write.len + RS485_FIXED_LEN);
    hal_uart_write(RS485_UART_PORT, write.frame, write.len + RS485_FIXED_LEN + 1);
}

#define RS485_MASTER_ADDR  0x80     /*!< RS485主机地址 */
#define RS485_SLAVE_NUM    4        /*!< RS485从机总数 */
#define RS485_SLAVE_ADDR   0x82     /*!< RS485从机地址 */

static bool slave_conn_status[8] = { 0 };
bool hal_rs485_slave_conn_status(uint8_t addr)
{
    return slave_conn_status[(addr & 0x7F)];
}

 
void hal_rs485_init(void)
{
   
#if 0  // test...  
    while (1) {
        vTaskDelay(500);
        static uint8_t data = 0;
        data += 5;
        hal_rs485_write_bytes(RS485_SLAVE_ADDR, &data, 1);
    }
#endif    
}
