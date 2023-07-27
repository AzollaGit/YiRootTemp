 
/**
 * @file    hal_voice.c
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
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "hal_uart.h"

#include "hal_voice.h"

#define TAG "hal_voice"

#define AUDIO_UART_PORT   UART_NUM_2
#define AUDIO_BUSY_PIN    26
 
//============================================================================================
//============================================================================================
// KT404C 语音芯片
 
void voice_write_cmd(uint8_t cmd, uint8_t datah, uint8_t datal)
{
    voice_data_t frame;
    frame.data[0] = 0x7E;
    frame.data[1] = 0xFF;
    frame.data[2] = 0x06;
    frame.data[3] = cmd;
    frame.data[4] = 0x00;    // 0x00: 不需要反馈
    frame.data[5] = datah;  
    frame.data[6] = datal;   
    frame.data[7] = 0xEF;   
    frame.len = 8;
    hal_uart_write(AUDIO_UART_PORT, frame.data, frame.len);
}

void voice_set_vol(uint8_t vol)
{
    voice_write_cmd(0x06, 0x00, vol); // 设置音量 
}

bool voice_is_busy(void)
{
    return gpio_get_level(AUDIO_BUSY_PIN);
}

// 0x0F 指定文件夹文件名播放
void hal_voice_speech(uint8_t voice)
{
    // voice_write_cmd(0x0F, dir, voice);  // dir: 文件夹01; voice: 曲目
    voice_write_cmd(0x0F, 0x01, voice);  // dir: 文件夹01; voice: 曲目
}
 
void hal_voice_init(void)
{
    // gpio_set_direction(AUDIO_BUSY_PIN, GPIO_MODE_INPUT);
    // gpio_pullup_en(AUDIO_BUSY_PIN);
    // vTaskDelay(300);
    // voice_set_vol(3);  // 设置音量！
    // vTaskDelay(300);
    // hal_voice_speech(VOICE_COMM_POWER_ON);  // 开机声
#if 0  // Test.
    uint8_t voice_file = 1;
    while (1) {
        vTaskDelay(3000);
        hal_voice_speech(voice_file++);
        if (voice_file > 13) voice_file = 1;
    }
#endif    
}
