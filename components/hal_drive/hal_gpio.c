/**
 * @file    hal_gpio.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   
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
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "hal_config.h"
#include "hal_gpio.h"
  
//=============================================================================
 
//=============================================================================
 
void gpio_output_init(void)
{
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g. 
    io_conf.pin_bit_mask = BIT64(3) | BIT64(40);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}

void gpio_input_init(void)
{
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g. 
    io_conf.pin_bit_mask = BIT64(21) | BIT64(0) | BIT64(5) | BIT64(6) | BIT64(38) | BIT64(39);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 1;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}
 
void hal_gpio_init(void)
{
    esp_log_level_set("gpio", ESP_LOG_WARN);
    gpio_output_init();
    gpio_input_init(); 
}
 
 