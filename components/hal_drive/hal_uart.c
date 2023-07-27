/* Uart Events Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

#include "hal_config.h"

#include "hal_uart.h"
 
#define TAG  "hal_uart"
 
// Timeout threshold for UART = number of symbols (~10 tics) with unchanged state on receive pin
#define UART_READ_TOUT  (3) // 3.5T * 8 = 28 ticks, TOUT=3 -> ~24..33 ticks

typedef struct {
    int txd_pin;
    int rxd_pin;
    int rts_pin;
    int cts_pin;
    uint16_t buff_size;
    uart_port_t   port;
    uart_mode_t   mode;
    uart_config_t config;
} hal_uart_config_t;

typedef struct {
    uart_port_t port;
    uint16_t buff_size;
} hal_uart_read_t;
 
static uart_recv_callback_t uart_recv_callback_func = NULL; 

// UART订阅接收消息服务回调函数
void uart_recv_register_callback(uart_recv_callback_t cb_func)
{
    uart_recv_callback_func = cb_func;
}

static void hal_uart_write_bytes(const uart_port_t uart_port, const uint8_t *data, uint16_t len)
{
    if (uart_write_bytes(uart_port, data, len) != len) {
        // add your code to handle sending failure here
    } else {
        ESP_ERROR_CHECK( uart_wait_tx_done(uart_port, 100) );  
    }
}

void hal_uart_write(const uart_port_t uart_port, const uint8_t *data, uint16_t len)
{
#ifdef TAG
    char uart_tag[32];
    sprintf(uart_tag, "uart_write[%d]", uart_port);
    esp_log_buffer_hex(uart_tag, data, len);
#endif
    hal_uart_write_bytes(uart_port, data, len);
}

static void uart_read_task(void *arg)
{
    hal_uart_read_t *uart_arg = (hal_uart_read_t *)arg;
#ifdef TAG
    ESP_LOGI(TAG, "<uart_read_task>, UART[%d]->buff_size = %d \r\n", uart_arg->port, uart_arg->buff_size);
#endif 
    // Allocate buffers for UART
    uint8_t *read_buff = malloc(uart_arg->buff_size);
    while ( true ) {
        // Read data from UART
        int len = uart_read_bytes(uart_arg->port, read_buff, uart_arg->buff_size, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            uart_recv_callback_func(uart_arg->port, read_buff, len);
        }  
    }
}

static void hal_uart_driver_install(hal_uart_config_t uart_cfg)
{
    if (uart_cfg.buff_size < 128) uart_cfg.buff_size = 128;  
    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(uart_cfg.port, uart_cfg.buff_size * 2, 0, 0, NULL, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_cfg.port, &uart_cfg.config));
    // Set UART pins as per KConfig settings
    ESP_ERROR_CHECK(uart_set_pin(uart_cfg.port, uart_cfg.txd_pin, uart_cfg.rxd_pin, uart_cfg.rts_pin, uart_cfg.cts_pin));
    // Set RS485 half duplex mode
    ESP_ERROR_CHECK(uart_set_mode(uart_cfg.port, uart_cfg.mode));
    // Set read timeout of UART TOUT feature
    ESP_ERROR_CHECK(uart_set_rx_timeout(uart_cfg.port, UART_READ_TOUT));
    // Creating UART Recvice task
    static hal_uart_read_t uart_arg;
    uart_arg.port = uart_cfg.port;
    uart_arg.buff_size = uart_cfg.buff_size;
    char task_name[32];
    sprintf(task_name, "uart_read_task[%d]", uart_arg.port);
    xTaskCreatePinnedToCore(uart_read_task, task_name, 3 * 1024, (void *)&uart_arg, 8, NULL, APP_CPU_NUM);   
}
 
void hal_uart_init(uart_recv_callback_t cb_func)
{
    uart_recv_register_callback(cb_func);  // 先注册接收回调函数！
    hal_uart_config_t uart;
    uart.txd_pin = CONFIG_GPIO_AUDIO_TXD;  // 语音
    uart.rxd_pin = CONFIG_GPIO_AUDIO_RXD;
    uart.rts_pin = UART_PIN_NO_CHANGE;
    uart.cts_pin = UART_PIN_NO_CHANGE;
    uart.port = UART_NUM_2;
    uart.mode = UART_MODE_UART;
    uart.buff_size = 128;
    uart.config.baud_rate = 9600;
    uart.config.data_bits = UART_DATA_8_BITS;
    uart.config.parity    = UART_PARITY_DISABLE;
    uart.config.stop_bits = UART_STOP_BITS_1;
    uart.config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart.config.source_clk= UART_SCLK_APB;
    uart.config.rx_flow_ctrl_thresh = 64;
    hal_uart_driver_install(uart);
    // vTaskDelay(1000);
    uart.txd_pin = CONFIG_GPIO_RS485_TXD;   // RS485
    uart.rxd_pin = CONFIG_GPIO_RS485_RXD;
    uart.port = UART_NUM_1;
    uart.mode = UART_MODE_RS485_HALF_DUPLEX;
    uart.config.baud_rate = 115200;
    hal_uart_driver_install(uart);
}