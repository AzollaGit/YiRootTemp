/* *
 * @file    hal_spiffs.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   SPI Flash file system
 * @version 0.1
 * @date    2023-3-01
 * 
 * @copyright Copyright (c) 2023
 * */
#ifndef __HAL_SPIFFS_H__
#define __HAL_SPIFFS_H__

#ifdef __cpluspuls
extern "C" {
#endif

#include "dirent.h" 
#include <sys/unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "esp_spiffs.h"

#define FAT_MOUNT_POINT     "/spiffs"

void hal_spiffs_init(void);

size_t spiffs_read_file_size(const char *fpath);

FILE *spiffs_open_file(uint8_t findex, size_t *fsize);
 
#ifdef __cpluspuls
}
#endif

#endif /* __APP_SPIFFS_H__ END. */
