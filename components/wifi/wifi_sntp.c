/**
 * @file    wifi_sntp.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   using LwIP SNTP module and time functions
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
 
#include "wifi_init.h"
#include "wifi_sntp.h"

#define TAG   "wifi_sntp"

#define SNTP_SERVER_ENABLE  0

// 自定义校准本地时间同步！
// lwip/include/apps/esp_sntp.h:81:6: note: previous declaration of 'sntp_sync_time' was here
void sntp_sync_time(struct timeval *tv)  
{
    ESP_LOGI(TAG, "synchronization[custom] -> tv_sec: %lld", tv->tv_sec);
    settimeofday(tv, NULL);
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    setenv("TZ", "CST-8", 1);
    tzset();
}
 
void sntp_sync_time_custom(time_t timestamp)
{
    struct timeval tv = { 0 };
    tv.tv_sec = timestamp + 28800;  // +8小时时区(8 * 3600)
    sntp_sync_time(&tv); 
}

struct tm* sntp_get_time(void)
{
    time_t now = 0;
    time(&now); // update 'now' variable with current time
    struct tm *sntp = gmtime(&now);
    sntp->tm_year += 1900;
    sntp->tm_mon  += 1;
    // sntp->tm_hour  = (sntp->tm_hour + 8) % 24;
    sntp->tm_hour  = sntp->tm_hour;
    sntp->tm_wday  = (sntp->tm_wday + 6) % 7 + 0;   // 星期0~6
    #if 0 // #ifdef TAG
    ESP_LOGI("app_sntp", "%d-%02d-%02d %02d:%02d:%02d", 
                            sntp->tm_year,
                            sntp->tm_mon,
                            sntp->tm_mday,
                            sntp->tm_hour,
                            sntp->tm_min,
                            sntp->tm_sec);
    #endif
    return sntp;
}

void sntp_print_time(void)
{
    time_t now = 0;
    time(&now); // update 'now' variable with current time
    ESP_LOGI(TAG, "localtime -> tv_sec: %lld", now);

    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "sntp localtime : %s", strftime_buf);
}

#if SNTP_SERVER_ENABLE
// menuconfig -> Component config -> LWIP -> SNTP -> [30000] Request interval to update time (ms).
void time_sync_notification_cb(struct timeval *tv)
{
    // Set timezone to China Standard Time
    ESP_LOGI(TAG, "synchronization[sntp] -> tv_sec: %ld", tv->tv_sec);

    setenv("TZ", "CST-8", 1);
    tzset();
    // sntp_stop();  // 获取到同步时间后，暂停SNTP服务器
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL); 
    sntp_setservername(0, "ntp1.aliyun.com");
    sntp_setservername(1, "210.72.145.44");     // 国家授时中心服务器 IP 地址
    sntp_setservername(2, "pool.ntp.org");
    // sntp_setservername(2, "1.cn.pool.ntp.org");    
    // https://www.jianshu.com/p/6a98909a06d5
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

void app_sntp_task(void *arg)
{ 
    mwifi_connect_status(portMAX_DELAY);  // 先等待WIFI连接上
#ifdef LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1);
#endif
    initialize_sntp();

#if 1  // test...
    while (1) {
        sntp_print_time();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }  
#endif  
    vTaskDelete(NULL);
}

void wifi_sntp_init(void)
{
    xTaskCreatePinnedToCore(app_sntp_task, "sntp", 3 * 1024, NULL, 5, NULL, APP_CPU_NUM); 
#if 0
    #include "freertos/timers.h"
    TimerHandle_t timer = NULL;  
    timer = xTimerCreate("sntp_timercb", 1000 / portTICK_RATE_MS, true, NULL, sntp_timercb);
    if (timer != NULL) {  // 定时器创建成功
        xTimerStart(timer, 0);
    } 
#endif    
}
#endif
