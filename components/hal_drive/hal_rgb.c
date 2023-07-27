/* *
 * @file    hal_rgb.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   WS2812
 * @version 0.1
 * @date    2022-09-19
 * 
 * @copyright Copyright (c) 2022
 * */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"  
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
 
#include "hal_rgb.h" 

#if 1 
 
static const char *TAG = "hal_rgb";

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      35
#define RMT_LED_STRIP_NUMBERS       6   // WS2812个数

static QueueHandle_t xQueue = NULL;

/**********************************************************************
24bit 数据结构
G7 G6 G5 G4 G3 G2 G1 G0 R7 R6 R5 R4 R3 R2 R1 R0 B7 B6 B5 B4 B3 B2 B1 B0
*注：高位先发，按照 GRB 的顺序发送数据。
***********************************************************************/
// T0H 0 code, high voltage time        220ns~380ns
// T1H 1 code, high voltage time        580ns~1µs
// T0L 0 code, low voltage time         580ns~1µs
// T1L 1 code, low voltage time         580ns~1µs
// RES Frame unit, low voltage time     >280µs
#define WS2812_T0H_NS       (3)     // 300ns; 时钟是10MHz
#define WS2812_T0L_NS       (7)     
#define WS2812_T1H_NS       (7)
#define WS2812_T1L_NS       (3)
#define WS2812_RESET_US     (300)   

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
static void led_strip_hsv2rgb(hsv_t hsv, rgb_t *rgb)
{
    hsv.hue %= 360; // h -> [0,360]
    uint16_t rgb_max = hsv.value * 2.55f;
    uint16_t rgb_min = rgb_max * (100 - hsv.saturation) / 100;
 
    // RGB adjustment amount by hue
    uint16_t rgb_adj = (rgb_max - rgb_min) * (hsv.hue % 60) / 60;

    switch (hsv.hue / 60) {
    case 0:
        rgb->red   = rgb_max;
        rgb->green = rgb_min + rgb_adj;
        rgb->blue  = rgb_min;
        break;
    case 1:
        rgb->red   = rgb_max - rgb_adj;
        rgb->green = rgb_max;
        rgb->blue  = rgb_min;
        break;
    case 2:
        rgb->red   = rgb_min;
        rgb->green = rgb_max;
        rgb->blue  = rgb_min + rgb_adj;
        break;
    case 3:
        rgb->red   = rgb_min;
        rgb->green = rgb_max - rgb_adj;
        rgb->blue  = rgb_max;
        break;
    case 4:
        rgb->red   = rgb_min + rgb_adj;
        rgb->green = rgb_min;
        rgb->blue  = rgb_max;
        break;
    default:
        rgb->red   = rgb_max;
        rgb->green = rgb_min;
        rgb->blue  = rgb_max - rgb_adj;
        break;
    }
}
 
//================================================================================================
//================================================================================================
/**
 * @brief Type of led strip encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
    case 0: // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    ESP_GOTO_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for led strip encoder");
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    // different led strip might have its own timing requirements, following parameter is for WS2812
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 0.3 * config->resolution / 1000000, // T0H=0.3us
            .level1 = 0,
            .duration1 = 0.7 * config->resolution / 1000000, // T0L=0.9us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 0.7 * config->resolution / 1000000, // T1H=0.9us
            .level1 = 0,
            .duration1 = 0.3 * config->resolution / 1000000, // T1L=0.3us
        },
        .flags.msb_first = 1 // WS2812 transfer bit order: G7...G0R7...R0B7...B0
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    uint32_t reset_ticks = config->resolution / 1000000 * 50 / 2; // reset code duration defaults to 50us
    led_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}
//================================================================================================
//================================================================================================
static rmt_encoder_handle_t led_encoder = NULL;
void rmt_rgb_task(void *arg)
{
    rmt_channel_handle_t led_chan = (rmt_channel_handle_t)arg;
    uint8_t led_strip_pixels[RMT_LED_STRIP_NUMBERS * 3] = { 0 };
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    hsv_t hsv = { 0 }; 
    rgb_t rgb = { 0 };
    // 开机流水灯效果
    for (int i = -1; i < RMT_LED_STRIP_NUMBERS; i++) {
        if (i == 1 || i == 4) led_strip_pixels[i * 3 + 0] = 0xff;
        if (i == 0 || i == 3) led_strip_pixels[i * 3 + 1] = 0xff;
        if (i == 2 || i == 5) led_strip_pixels[i * 3 + 2] = 0xff;
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(500)));
        vTaskDelay(200);
    }
    while (1) { 
        if (xQueueReceive(xQueue, &hsv, portMAX_DELAY) == pdTRUE) {  
            led_strip_hsv2rgb(hsv, &rgb); // Build RGB values
            for (uint8_t i = 0; i < RMT_LED_STRIP_NUMBERS; i++) {
                led_strip_pixels[i * 3 + 0] = rgb.green;
                led_strip_pixels[i * 3 + 1] = rgb.red;
                led_strip_pixels[i * 3 + 2] = rgb.blue;
            }
            // Flush RGB values to LEDs
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(500)));
        }
    }
}

// HSV 色彩模型（Hue 色度, Saturation 饱和度, Value纯度,亮度）
void hal_rgb_set_hsv(hsv_t hsv)
{
    xQueueSend(xQueue, &hsv, (TickType_t)0); 
}
 
void hal_rgb_init(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 48, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 2, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_ERROR_CHECK(rmt_enable(led_chan));

    xQueue = xQueueCreate(tx_chan_config.trans_queue_depth, sizeof(hsv_t));
    xTaskCreatePinnedToCore(rmt_rgb_task, "rmt_rgb_task", 3 * 1024, led_chan, 7, NULL, APP_CPU_NUM);   
 
#if 0  // test...
    hsv_t hsv;
    while (1) {
        hsv.saturation = 100;
        hsv.value = 30;
        hsv.hue += 30; 
        hal_rgb_set_hsv(hsv);
        printf("hsv.hue = %d\n", hsv.hue);
        vTaskDelay(1900);
    }
#endif    
}

#endif

