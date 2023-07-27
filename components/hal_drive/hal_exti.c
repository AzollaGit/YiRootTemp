/**
 * @file    hal_exti.c
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
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "hal_config.h"
#include "hal_exti.h"

 
#define CONFIG_IO_EXTI0             48

#define GPIO_INPUT_PIN_SEL          BIT64(CONFIG_IO_EXTI0)

#define ESP_INTR_FLAG_DEFAULT       0

#define EXTI_NUM    1
static const gpio_num_t exti_gpio[EXTI_NUM] = {CONFIG_IO_EXTI0};

static QueueHandle_t xQueue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(xQueue, &gpio_num, NULL);
}

bool lcd_tearing_effect_wait(uint32_t time)
{
    uint32_t io_num = 0;
    xQueueReset(xQueue);
    xQueueReceive(xQueue, &io_num, time);
    return gpio_get_level(io_num);
}
 
static void gpio_intr_task(void* arg)
{
    static uint32_t io_num = 0;
    while (1) {
        if (xQueueReceive(xQueue, &io_num, portMAX_DELAY)) {
            ESP_LOGI("exit", "GPIO[%ld] intr, val: %d\r\n", io_num, gpio_get_level(io_num));
            // printf("GPIO[%d] intr, val: %d\r\n", io_num, gpio_get_level(io_num)); 
        }
    }
}


void hal_exti_init(void)
{
    gpio_config_t io_conf;

    //interrupt of rising edge
    // io_conf.intr_type = GPIO_INTR_NEGEDGE;   // 下降沿
    io_conf.intr_type = GPIO_INTR_POSEDGE;  // GPIO_INTR_POSEDGE
    //bit mask of the pins, use GPIOn here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //enable pull-up mode
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // change gpio intrrupt type for one pin
    // gpio_set_intr_type(CONFIG_IO_EXTI1, GPIO_INTR_ANYEDGE);

    // install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    for (uint8_t i = 0; i < EXTI_NUM; i++) {   
        // hook isr handler for specific gpio pin
        gpio_isr_handler_add(exti_gpio[i], gpio_isr_handler, (void*) exti_gpio[i]);
    }

    // create a queue to handle gpio event from isr
    xQueue = xQueueCreate(1, sizeof(uint32_t));
    
    // start gpio task
    xTaskCreate(gpio_intr_task, "gpio_intr_task", 3 * 1024, NULL, configMAX_PRIORITIES - 3, NULL); 
}
