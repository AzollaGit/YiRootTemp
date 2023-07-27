/**
 * @file    mac_utils.c
 * @author  Azolla (1228449928@qq.com)
 * @brief
 * @version 0.1
 * @date    2022-10-19
 * 
 * @copyright Copyright (c) 2022
 * */
#include <stdio.h>
#include <string.h>

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "mac_utils.h"

#define STR2HEX(X)   (((X)[0] - '0') << 4) | ((X)[1] - '0')

 
uint8_t *mac_utils_str2hex(const char *mac_str, uint8_t *mac_hex)
{
    uint32_t mac_data[6] = {0};
    sscanf(mac_str, ADDRSTR, mac_data, mac_data + 1, mac_data + 2, mac_data + 3, mac_data + 4, mac_data + 5);
    for (int i = 0; i < 6; i++) {
        mac_hex[i] = mac_data[i];
    }
    return mac_hex;
}

char *mac_utils_hex2str(const uint8_t *mac_hex, char *mac_str)
{
    sprintf(mac_str, ADDRSTR, MAC2STR(mac_hex));
    return mac_str;
}

uint8_t *mac_utils_ap2sta(const uint8_t *ap_mac, uint8_t *sta_mac)
{
    memcpy(sta_mac, ap_mac, 2);
    *((int *)(sta_mac + 2)) = htonl(htonl(*((int *)(ap_mac + 2))) - 1);
    return sta_mac;
}

uint8_t *mac_utils_bt2sta(const uint8_t *bt_mac, uint8_t *sta_mac)
{
    memcpy(sta_mac, bt_mac, 2);
    *((int *)(sta_mac + 2)) = htonl(htonl(*((int *)(bt_mac + 2))) - 2);
    return sta_mac;
}