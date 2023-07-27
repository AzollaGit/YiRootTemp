#ifndef __HAL_RS485__
#define __HAL_RS485__

#include "hal_config.h"
#include "driver/uart.h"

 
// typedef struct {
//     uint8_t addr;
//     uint8_t data[64];
//     uint8_t len;
// } rs485_write_t;

typedef union {
#define RS485_HEAD       0XA5    // 固定头
#define RS485_DATA_LEN   128     // 数据长度
#define RS485_FIXED_LEN  4       // 固定长度 
#define RS485_CMD_READ   0X01    // 主机读指令/需要应答
#define RS485_CMD_WRITE  0X02    // 主机写指令/需要应答
#define RS485_CMD_NOTIF  0X04    // 主机通知指令/不需要应答
#define RS485_CMD_HB     0X08    // 主机心跳包指令/不需要应答
    struct {
        uint8_t head;
        uint8_t addr;
        uint8_t cmd;
        uint8_t len;
        uint8_t data[RS485_DATA_LEN];
        uint8_t xor;
    };
    uint8_t frame[RS485_FIXED_LEN + RS485_DATA_LEN + 1];
} rs485_format_t;
 
void hal_rs485_init(void);

void hal_rs485_write(uint8_t addr, const uint8_t *data, uint8_t len);

#endif /* __APP_RS485__ END. */

