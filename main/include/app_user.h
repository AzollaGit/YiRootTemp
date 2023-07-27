#ifndef __APP_USER_H__
#define __APP_USER_H__

#include "hal_config.h"
#include "hal_nvs.h"
#include "hal_timer.h"
#include "hal_exti.h"
#include "hal_gpio.h"
#include "hal_uart.h"
#include "hal_spiffs.h"
#include "hal_rs485.h"
#include "hal_voice.h"
#include "hal_rgb.h"
#include "hal_usb_msc.h"

#include "cJSON.h"

#define APP_PARSE_NULL  0x00
#define APP_PARSE_ROOT  0x01
#define APP_PARSE_NODE  0x02
#define APP_PARSE_EXIT  0xFF   // 退出 

typedef struct {
#define ARRAY_TYPE  uint8_t     /* 定义[void *value] 为数组类型时的数据类型 */
    uint8_t addr[6];     // mesh mac_addr
    uint16_t dst_addr;   // mesh 单播/组地址
    uint32_t opcode;     // mesh 操作码
    char    *name;
    void    *value;
    cJSON   *json;       // 存放原始JSON的
    uint16_t size;
} app_parse_data_t;
 
void app_user_init(void); 

#endif
