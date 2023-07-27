/* SD card and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)
 
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
 
// This example uses SDMMC peripheral to communicate with SD card.
#if 1
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

// #define TAG  "usb_msc"
 
static int usb_itf = -1;
static uint8_t usb_rx_buff[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];

static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, usb_rx_buff, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        usb_rx_buff[rx_size] = '\0';
        printf("rx_buf[%s]\n", usb_rx_buff);
        /* write back */
        tinyusb_cdcacm_write_queue(itf, usb_rx_buff, rx_size);
        tinyusb_cdcacm_write_flush(itf, 0);
    } else {
        #ifdef TAG 
        ESP_LOGE(TAG, "Read error");
        #endif
    }
}

static void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
#ifdef TAG    
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);
#endif 
}


static void tinyusb_cdc_line_coding_changed_callback(int itf, cdcacm_event_t *event)
{
    usb_itf = itf;  // 保存，发送用
#ifdef TAG
    static cdc_line_coding_t line_coding; // 上一次的参数
    // 实测发现回调会连续执行4次，所以这里做一个对比，防止是一样的多次执行以下代码
    if (memcmp(&line_coding, event->line_coding_changed_data.p_line_coding, sizeof(cdc_line_coding_t))) {
        memcpy(&line_coding, event->line_coding_changed_data.p_line_coding, sizeof(cdc_line_coding_t));
        // line_coding.bit_rate  = event->line_coding_changed_data.p_line_coding->bit_rate;
        // line_coding.stop_bits = event->line_coding_changed_data.p_line_coding->stop_bits;  
        // line_coding.parity    = event->line_coding_changed_data.p_line_coding->parity;
        // line_coding.data_bits = event->line_coding_changed_data.p_line_coding->data_bits;
        #ifdef TAG 
        printf("--------------------------------------\r\n");
        printf("bit_rate : %ld\r\n", line_coding.bit_rate);
        printf("stop_bits: %d\r\n", line_coding.stop_bits + 1);
        printf("parity   : %d\r\n", line_coding.parity);
        printf("data_bits: %d\r\n", line_coding.data_bits);
        printf("======================================\r\n");
        #endif
    }
#endif 
}

 
void usb_cdc_init(void)
{
    tinyusb_config_cdcacm_t amc_cfg = { 
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 1024,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed  = tinyusb_cdc_line_state_changed_callback,
        .callback_line_coding_changed = tinyusb_cdc_line_coding_changed_callback,
    };  

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&amc_cfg));
 
#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_1,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &tinyusb_cdc_line_state_changed_callback));
#endif

    #ifdef TAG 
    ESP_LOGI(TAG, "USB initialization cdc device DONE");
    #endif
}
 
void usb_printf(const char *fmt, ...) 
{
    if (usb_itf < 0) return;
    char usb_tx_buff[CONFIG_TINYUSB_CDC_TX_BUFSIZE + 1] = { 0 };
    va_list ap;
    va_start(ap, fmt);          	     
    vsprintf(usb_tx_buff, fmt, ap);
    va_end(ap);
    #ifdef TAG 
    printf("usb_printf = %s | %d\r\n", usb_tx_buff, strlen(usb_tx_buff));
    #endif
    tinyusb_cdcacm_write_queue(usb_itf, (const uint8_t *)usb_tx_buff, strlen(usb_tx_buff)); 
    tinyusb_cdcacm_write_flush(usb_itf, 0);
}
 
void hal_usb_cdc_init(void)
{
    usb_cdc_init();
#if 0  // test... 
    vTaskDelay(3000 / portTICK_PERIOD_MS);  // 等待
    while(1) {
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        usb_printf("test\r\n") ;
    }
#endif 
}
#endif