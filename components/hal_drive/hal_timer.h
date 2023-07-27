#pragma once

#include "stdint.h"
#include "esp_timer.h"

esp_timer_handle_t esp_timer_periodic_create(esp_timer_cb_t timer_cb, const char* timer_name, uint64_t timeout_us);

esp_timer_handle_t esp_timer_once_create(esp_timer_cb_t timer_cb, const char* timer_name, uint64_t timeout_us);