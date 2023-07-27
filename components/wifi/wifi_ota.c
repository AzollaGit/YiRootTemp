/**
 * @file    wifi_mupgrade.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#include <stdio.h>
#include <stdlib.h>
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_log.h"
 

#if (ESP_IDF_VERSION_MAJOR >= 5)
#include "esp_mac.h"
#include "lwip/ip_addr.h"
#endif

 
#include "wifi_ota.h"

#if 0
#define IDF_ERROR_GOTO(con, lable, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGE(TAG, format, ##__VA_ARGS__); \
            goto lable; \
        } \
    } while(0)
IDF_ERROR_GOTO(ret != ESP_OK, EXIT,"esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(ret));
#endif
 
// #define TAG "WiFi_OTA"
 
#define APP_CONFIG_VERSION_CHECK               0 
#define APP_CONFIG_PROJECT_NAME_CHECK          1 
// #define APP_CONFIG_FIRMWARE_UPGRADE_URL        "http://git.hgoo.cc:8882/bamboo/s2/hgooo-bamboo.bin" 
 
uint16_t esp_read_chip_id(void)
{
    chip_id_t chipid;
    esp_flash_read_unique_chip_id(NULL, &chipid.u64);
    uint16_t uid = (chipid.u16[0] ^ chipid.u16[1]) + (chipid.u16[2] ^ chipid.u16[3]);
    #if 0   
    ESP_LOGI("main", "chipid = %lld", chipid.u64);
    ESP_LOGI("main", "uid = %05d", uid);
    #endif
    return uid;
}

uint8_t esp_read_chip_name(const char *dev_name, char *name)
{
    return sprintf(name, "%s%05d", dev_name, esp_read_chip_id());
}

void app_get_self_info(self_info_t *self)
{
    esp_read_mac(self->addr, ESP_MAC_BT);   
    esp_read_chip_name("YiRoot",self->dev_name);  // 读取设备名
    const esp_partition_t *running = esp_ota_get_running_partition();  // 获取当前运行固件信息（版本|固件名...）
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        memcpy(self->bin_name, running_app_info.project_name, strlen(running_app_info.project_name));
        memcpy(self->version, running_app_info.version, strlen(running_app_info.version));
        #if 0
        ESP_LOGI("bin", "Running firmware version: %s", running_app_info.version);
        ESP_LOGI("bin", "Running firmware project_name: %s", running_app_info.project_name);
        #endif
    }
    #ifdef TAG  
    ESP_LOGI(TAG, "chip_name: %s", self->dev_name);
    #endif
}

// 恢复到出厂分区运行！
esp_err_t esp_ota_reset_factory(void)
{ 
    const esp_partition_t *partition = NULL;
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (partition == NULL) {
        partition = esp_ota_get_next_update_partition(NULL);
    }
    return esp_ota_set_boot_partition(partition);
}

//=================================================================================
//=================================================================================
static uint8_t ota_addr[6];
static bool  ota_is_init = 0;
static short ota_update_plan = 0;  // OTA进度
// 返回OTA进度
int wifi_ota_update_plan(void)
{
    if (ota_update_plan < 0) {
        ota_update_plan = 0;
        return - 1;  // OTA ERR.
    }
    return ota_update_plan;
}

uint8_t *app_get_ota_addr(void)
{
    return ota_addr;
}

// static portMUX_TYPE ota_lock = portMUX_INITIALIZER_UNLOCKED;
// portENTER_CRITICAL(&ota_lock);
// portEXIT_CRITICAL(&ota_lock);
 
#if 1
// ftp://112.124.38.58:21
// gateway
// HPDr7tTiNe5mE6NL
static void wifi_ota_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    ota_packet_t *packet = heap_caps_malloc(sizeof(ota_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(packet);
    int total_size  = 0;
    /* update_handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
  
    ota_update_plan = 0;  // OTA进度
    self_info_t self_info = { 0 };
    app_get_self_info(&self_info);   // 获取当前运行固件的信息

    bool boot_is_upgrade = false;
    if (memcmp(ota_addr, self_info.addr, 6) == 0 ) {  // 是ROOT节点升级
        boot_is_upgrade = true;
        update_partition = esp_ota_get_next_update_partition(NULL);
        assert(update_partition != NULL);
        #ifdef TAG  
        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x", update_partition->subtype, update_partition->address);
        #endif
    } 

    esp_http_client_config_t config = {
        .url = (const char *)arg,   // http_url地址
        .timeout_ms = 5000,
        .keep_alive_enable = true,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
    };
 
    #ifdef TAG  
    ESP_LOGI(TAG, "Starting WIFI Mesh OTA  ...");
    ESP_LOGI(TAG, "Open HTTP connection: %s", config.url);
    TickType_t ota_start_time = xTaskGetTickCount();
    #endif

    /**
     * @brief 1. Connect to the server
     */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (ret != ESP_OK) {
        #ifdef TAG     
        ESP_LOGE(TAG, "Initialise HTTP connection");
        #endif
        goto EXIT;
    }

    ret = esp_http_client_open(client, 0); 
    if (ret != ESP_OK) {
        #ifdef TAG     
        ESP_LOGE(TAG, "<%s> Connection service failed", mdf_err_to_name(ret));
        #endif
        goto EXIT;
    }

    total_size = esp_http_client_fetch_headers(client);
    #ifdef TAG  
    ESP_LOGI(TAG, "total_size = %d", total_size);
    #endif
    if (total_size <= 0) {
        #ifdef TAG     
        ESP_LOGW(TAG, "Please check the address of the server");
        #endif
        goto EXIT;
    }

    /**
     * @brief 3. Read firmware from the server and write it to the flash of the root node
     */
    bool image_header_was_checked = false;
    
    for (size_t size = 0, recv_size = 0; recv_size < total_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)packet->data, OTA_PACKET_SIZE);
        ota_update_plan = ((float)recv_size / total_size) * 100; // 计算OTA百分比进度！     
        if (size > 0) { // read ok!
#if APP_CONFIG_PROJECT_NAME_CHECK || APP_CONFIG_VERSION_CHECK            
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (size > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &packet->data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    #ifdef TAG  
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);
                    ESP_LOGI(TAG, "New firmware project_name: %s", new_app_info.project_name);
                    #endif
#if APP_CONFIG_PROJECT_NAME_CHECK   // check project_name                
                    if (memcmp(new_app_info.project_name, self_info.bin_name, strlen(self_info.bin_name)) != 0) {
                        #ifdef TAG     
                        ESP_LOGW(TAG, "App project name error");
                        #endif
                        ret = ESP_FAIL;
                    }
#endif  /* APP_CONFIG_PROJECT_NAME_CHECK END */   
#if APP_CONFIG_VERSION_CHECK   // check version
                    // size_t new_version = atoi(new_app_info.version);  // 转成数字进行对比，或者版本号不一样就升级
                    if (memcmp(new_app_info.version, self_info.version, sizeof(new_app_info.version)) == 0) {
                        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                        ret = ESP_FAIL;
                    }
#endif  /* APP_CONFIG_VERSION_CHECK END */  
                    vTaskDelay(100);  // 延时一下，防止擦写时间过长，看门口复位
                    /**< Commence an OTA update writing to the specified partition. */
                    ret = esp_ota_begin(update_partition, total_size, &update_handle);  // 提前知道整个固件的大小，就先全部擦写，以后就直接写入
                    if (ret != ESP_OK) {
                        #ifdef TAG     
                        ESP_LOGE(TAG, "esp_ota_begin1 failed 0x%x(%s)", ret, esp_err_to_name(ret));
                        #endif
                        // esp_ota_mark_app_valid_cancel_rollback();  // 验证该固件是OK的
                    }
                    vTaskDelay(100);  // 延时一下，防止擦写时间过长，看门口复位
                } else {  // image_header_was_checked fail!
                    ret = ESP_FAIL;
                }       
                if (ret != ESP_OK) {
                    #ifdef TAG  
                    ESP_LOGE(TAG, "esp_ota_begin2 failed 0x%x(%s)", ret, esp_err_to_name(ret));
                    #endif  
                    goto EXIT;
                }    
                image_header_was_checked = true;  // 开始OTA
            }     
#endif  /* APP_CONFIG_PROJECT_NAME_CHECK || APP_CONFIG_VERSION_CHECK END */  
            /* @brief  Write firmware to flash */
            ret = esp_ota_write( update_handle, (const void *)packet->data, size);
            if (ret != ESP_OK) {
                #ifdef TAG  
                ESP_LOGE(TAG, "esp_ota_write ERR <%s>", esp_err_to_name(ret));
                #endif  
                goto EXIT;
            }            
        } else if (size == 0) {
            #ifdef TAG     
            if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
            } 
            #endif
            break;
        } else {  // if (size < 0) // http read error.
            #ifdef TAG     
            ESP_LOGE(TAG, "<%s> Read data from http stream", mdf_err_to_name(ret));
            #endif
            goto EXIT;
        }
    }      

    #ifdef TAG  
    MDF_LOGI("The service download firmware is complete, Spend time: %ds",
             (xTaskGetTickCount() - ota_start_time) * portTICK_RATE_MS / 1000);
    #endif     

    ret = esp_http_client_is_complete_data_received(client);    
    if (ret != true) {
        #ifdef TAG  
        ESP_LOGE(TAG, "Error in receiving complete file");
        #endif  
        goto EXIT;
    }     

    if (update_handle != 0) {
        ret = esp_ota_end(update_handle);
        if (ret != ESP_OK) {
            #ifdef TAG     
            if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            } else {
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(ret));
            }
            #endif
            goto EXIT;
        }
    }

    if (update_partition != NULL) {
        ret = esp_ota_set_boot_partition(update_partition);
        if (ret != ESP_OK) {
            #ifdef TAG     
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(ret));
            #endif
            goto EXIT;
        }
    }
 
    if (boot_is_upgrade == false) {
        ota_update_plan = 200;  // OTA=200%, 用于区分是不是ROOT节点升级
    } else {
        ota_update_plan = 100;  // OTA=100%
    }
    #ifdef TAG  
    MDF_LOGI("\n\nOTA OK...  ->   esp_restart...\n\n");
    #endif
    // vTaskDelay(1000);
    // esp_restart();  
EXIT:
    if (ota_update_plan < 100) {
        #ifdef TAG  
        ESP_LOGW(TAG, "\n\nOTA Fail...\n\n");
        #endif
        ota_update_plan = -1;  // OTA失败了
    }  
     
    if (update_handle != 0) {
        esp_ota_abort(update_handle);
    }

    if (client != 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    free(packet);

    vTaskDelay(500);
    
    ota_update_plan = 0;
    ota_is_init = false;

    vTaskDelete(NULL);
}
#endif

void app_ota_start(uint8_t addr[6], const char *ota_url)
{
    if (ota_is_init == true) return;
    else ota_is_init = true;

    memcpy(ota_addr, addr, 6); 
    xTaskCreatePinnedToCore(wifi_ota_task, "wifi_ota", 5 * 1024, (void *)ota_url, configMAX_PRIORITIES - 5, NULL, PRO_CPU_NUM);    
    vTaskDelay(200); // 延时一下，传递ota_url
}


