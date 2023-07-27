/**
 * @file    hal_timer.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   led contorl PWM
 * @version 0.1
 * @date    2022-12-06
 * 
 * @copyright Copyright (c) 2022
 * */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "hal_timer.h"


//static const char *TAG = "APP_TIMER";
// esp_timer_periodic_create(timer_callback_fun, "timer1ms", 1000);
esp_timer_handle_t esp_timer_periodic_create(esp_timer_cb_t timer_cb, const char* timer_name, uint64_t timeout_us)
{
    esp_timer_handle_t timer_handle;

    const esp_timer_create_args_t create_args = {
        .callback = timer_cb,
        .name     = timer_name
    };

    ESP_ERROR_CHECK(esp_timer_create(&create_args, &timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle, timeout_us));
    return timer_handle;
}

esp_timer_handle_t esp_timer_once_create(esp_timer_cb_t timer_cb, const char* timer_name, uint64_t timeout_us)
{
    esp_timer_handle_t timer_handle;

    const esp_timer_create_args_t create_args = {
        .callback = timer_cb,
        .name     = timer_name
    };

    ESP_ERROR_CHECK(esp_timer_create(&create_args, &timer_handle));
    if (timeout_us) {
        ESP_ERROR_CHECK(esp_timer_start_once(timer_handle, timeout_us));
    } 
    // ESP_ERROR_CHECK(esp_timer_stop(timer_handle));  
    return timer_handle;
}

// int64_t esp_timer_get_time(void);   // 以us计数
// ESP_ERROR_CHECK(esp_timer_stop(timer_handle));

