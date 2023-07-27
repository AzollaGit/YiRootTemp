/* *
 * @file    hal_spiffs.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   SPI Flash file system
 * @version 0.1
 * @date    2023-3-01
 * 
 * @copyright Copyright (c) 2023
 * */
#include <string.h>
#include <stdio.h>
#include <string.h>
 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "hal_spiffs.h"

// #define TAG  "hal_spiffs"
 
// 打印文件目录
void spiffs_print_file_dir(void)
{
#ifdef TAG
    uint8_t i = 0;
    DIR *dir = NULL;  
    struct dirent *entry;  
    if ( (dir = opendir(FAT_MOUNT_POINT)) == NULL){  
        ESP_LOGE(TAG, "opendir failed!");  
        return;  
    }
    while ( (entry = readdir(dir)) != NULL) {  
        // 输出文件或者目录的名称, 文件类型   
        printf("filename[%d] = %s | %d\r\n", i++, entry->d_name, entry->d_type); 
    }  
    closedir(dir);    
#endif       
}

// 根据索引返回文件的路径/文件名
char *spiffs_read_file_name(uint8_t index)
{
    DIR *dir = NULL;  
    struct dirent *entry;   
    if ( (dir = opendir(FAT_MOUNT_POINT)) == NULL) {  
        #ifdef TAG
        ESP_LOGE(TAG, "opendir failed!");  
        #endif
        return NULL;  
    }
    seekdir(dir, index);
    entry = readdir(dir);
    closedir(dir);
    // printf("mp3_file = %s | %d\n", entry->d_name, index);   
    if ( entry != NULL) {  
        return entry->d_name; 
    } 
    return NULL;  
}

size_t spiffs_read_file_size(const char *fpath)
{
    struct stat st;
    stat(fpath, &st);
    return st.st_size; // read file size.
}

FILE *spiffs_open_file(uint8_t findex, size_t *fsize)
{
    const char *fname = spiffs_read_file_name(findex);  // 根据目录索引，找到对应的音乐路径！
    char fpath[32] = FAT_MOUNT_POINT"/"; // "/spiffs/music.mp3"
    memcpy(fpath + 8, fname, strlen(fname));
    #ifdef TAG
    ESP_LOGI(TAG, "file_fpath[%d]: %s", findex, fpath);
    #endif
    FILE *fb = fopen(fpath, "rw+");
    if (fb == NULL) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to open file for reading");
        #endif
        return NULL;
    }
 
    *fsize = spiffs_read_file_size(fpath);
 
    return fb;
}
 

// SPIFFS 是一个用于 SPI NOR flash 设备的嵌入式文件系统
// https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/storage/spiffs.html
// https://espressif-docs.readthedocs-hosted.com/projects/espressif-esp-iot-solution/zh_CN/latest/storage/file_system.html
void hal_spiffs_init(void)
{
    #ifdef TAG
    ESP_LOGI(TAG, "Initializing SPIFFS");
    #endif

#if 0  // 使能格式化
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
    if (part != NULL) {
        esp_spiffs_format(part->label);  // 格式化
    }
    return;
    // remove existing file
    unlink("/spiffs/test1.db");
#endif

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = FAT_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = CONFIG_SPIFFS_MAX_PARTITIONS,
        .format_if_mount_failed = true,
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        #ifdef TAG
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        #endif
        return;
    }

#if 0
    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(spiffs_conf.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }
#endif    

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        #endif    
        return;
    } else {
        #ifdef TAG        
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
        #endif
    }

#ifdef TAG
    spiffs_print_file_dir();
#endif

    // All done, unmount partition and disable SPIFFS
    // esp_vfs_spiffs_unregister(spiffs_conf.partition_label);
}
