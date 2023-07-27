/* *
 * @file    hal_rgb.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   WS2812
 * @version 0.1
 * @date    2022-09-19
 * 
 * @copyright Copyright (c) 2022
 * */
#ifndef __APP_RGB__
#define __APP_RGB__

#include "driver/gpio.h"

typedef struct {
    uint16_t hue;           // Hue 色度 
    uint8_t  saturation;    // Saturation 饱和度
    uint8_t  value;         // Value 纯度,亮度
} hsv_t;

typedef struct {
    uint8_t  red;
    uint8_t  green;
    uint8_t  blue;
} rgb_t;
 
void hal_rgb_set_hsv(hsv_t hsv);
 
void hal_rgb_init(void);

#endif /* __APP_RGB__ END. */


