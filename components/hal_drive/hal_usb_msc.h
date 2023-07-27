/**
 * @file hal_usb_msc.h
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-06-01
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef __HAL_USB_MSC_H__
#define __HAL_USB_MSC_H__

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
 
// #define MOUNT_POINT "/sdcard"
#define MOUNT_POINT "/spiffs"

void hal_usb_msc_init(void);

void hal_usb_cdc_init(void);
void usb_printf(const char *fmt, ...);

#endif  /* __HAL_USB_MSC_H__ end. */
