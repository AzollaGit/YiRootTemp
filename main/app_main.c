/**
 * @file    app_main.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version v0.1
 * @date    2022-08-19
 * 
 * @copyright Copyright (c) 2022
 * */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task_wdt.h"

#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
 
#include "app_user.h"

// #define TAG  "app_main"

#if 0
         █
         █
     ▄   █   ▄                                █     █
     ▀▀█████▀▀      ▄▀▀▀▄ ▄▀▀▀▄    ▄▀▀▀▀▄█  ▀▀█▀▀▀▀▀█▀▀   ▄▀▀▀▀▄    ▄▀▀
   ▀█▄       ▄█▀   █     █     █  █      █    █     █    █▄▄▄▄▄▄█  █
     ▀█▄   ▄█▀     █     █     █  █      █    █     █    █         █
  ▄██▀▀█   █▀▀██▄  █     █     █   ▀▄▄▄▄▀█    ▀▄▄   ▀▄▄   ▀▄▄▄▄▀   █
 ▀▀    █   █    ▀▀
#endif
// idf_v5.0 freertos版本介绍：
// https://github.com/espressif/esp-idf/blob/release/v5.0/components/freertos/FreeRTOS-Kernel/History.txt

/*****************************************************************************************************************
flash加密： https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/security/flash-encryption.html  
    Component config → ESP32S3-Specific → Support for external, SPI-connected RAM → SPI RAM config   
            [*]Cache fetch instructions from SPI RAM    // 不选择，SPI-SPRAM 会大很多，选择了至少减少2M大小！
            [ ]Cache load read only data from SPI RAM
)            
*****************************************************************************************************************/         

void app_main(void *args) 
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            if (1) {  // 默认确认
                // ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();  // 验证确认
            } else {
                // ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                // esp_ota_mark_app_invalid_rollback_and_reboot();  // 回滚
            }
        }
    }

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
 
    app_user_init();
}
