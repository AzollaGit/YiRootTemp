/**
 * @file    mac_utils.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2023-06-03
 * 
 * @copyright Copyright (c) 2023
 * */
#ifndef __MAC_UTILS_H__
#define __MAC_UTILS_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errno.h"
#include "sdkconfig.h"
 


#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#ifndef MACSTR
    #define MACSTR      "%02x:%02x:%02x:%02x:%02x:%02x"
    #define MAC2STR(a)  (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

#ifndef ADDRSTR
    #define ADDRSTR      "%02x%02x%02x%02x%02x%02x"
    #define ADDR2STR(a)  (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif
 
/**
 * @brief  Convert mac from string format to hex
 *
 * @param  mac_str String format mac
 * @param  mac_hex Hex format mac
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */
uint8_t *mac_utils_str2hex(const char *mac_str, uint8_t *mac_hex);

/**
 * @brief  Convert mac from hex format to string
 *
 * @param  mac_hex Hex format mac
 * @param  mac_str String format mac
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */
char *mac_utils_hex2str(const uint8_t *mac_hex, char *mac_str);

/**
 *  Each ESP32 chip has MAC addresses for Station (STA), Access Point (AP), Bluetooth low energy (BT) and Local Area Network (LAN).
 *  Their address values are incremented by one, i.e. LAN Mac = BT Mac + 1 = AP Mac + 2 = STA Mac + 3.
 *
 *  For example:
 *      - MAC for STA: `xx:xx:xx:xx:xx:00`
 *      - MAC for AP : `xx:xx:xx:xx:xx:01`
 *      - MAC for BT : `xx:xx:xx:xx:xx:02`
 *      - MAC for LAN: `xx:xx:xx:xx:xx:03`
 *
 *    The device's STA address is used For ESP-WIFI-MESH communication.
 **/

/**
 * @brief Convert mac address from ap to sta
 *
 * @param  ap_mac  Access Point address in hexadecimal format
 * @param  sta_mac Station address in hexadecimal format
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */
uint8_t *mac_utils_ap2sta(const uint8_t *ap_mac, uint8_t *sta_mac);

/**
 * @brief Convert mac address from bt to sta
 *
 * @param  bt_mac  Access Point address in hexadecimal format
 * @param  sta_mac Station address in hexadecimal format
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */
uint8_t *mac_utils_bt2sta(const uint8_t *bt_mac, uint8_t *sta_mac);

#ifdef __cplusplus
}
#endif /**< _cplusplus */

#endif /* __MAC_UTILS_H__ end. */