#ifndef __HAL_VOICE__
#define __HAL_VOICE__

#include "hal_config.h"
#include "driver/uart.h"

//文件夹01 -- 通用操作
#define VOICE_COMM_POWER_ON             1   //开机音乐
#define VOICE_COMM_WIFI_PAIR            2   //开始配网
#define VOICE_COMM_WIFI_PAIR_OK         3   //配网成功
#define VOICE_COMM_WIFI_PAIR_FAIL       4   //配网失败
#define VOICE_COMM_WIFI_CONNECT_FAIL    5   //网络连接失败
#define VOICE_COMM_WIFI_CONNECT_OK      6   //网络已连接
#define VOICE_COMM_WIFI_DISCONNECT      7   //网络未连接
#define VOICE_COMM_SERVER_CONNECT_FAIL  8   //服务器连接失败
#define VOICE_COMM_SERVER_CONNECT_OK    9   //服务器连接成功
#define VOICE_COMM_SERVER_DISCONNECT    10  //服务器断开
#define VOICE_COMM_WIFI_CONNECTING      11  //正在连接网络
#define VOICE_COMM_POWER_DOWN           12  //设备关机
#define VOICE_COMM_OTA                  13  //升级提示
#define VOICE_COMM_OTA_OK               14  //升级完成
 
typedef struct {
    uint8_t data[8];
    uint8_t len;
} voice_data_t ;
 
void hal_voice_init(void);
void voice_set_vol(uint8_t vol);
void hal_voice_speech(uint8_t voice);

#endif /* __HAL_VOICE__ END. */
