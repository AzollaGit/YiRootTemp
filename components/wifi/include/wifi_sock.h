
/**
 * @file    wifi_sock.h
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2023-02-06
 * 
 * @copyright Copyright (c) 2023
 * */
#ifndef __WIFI_SOCK_H__
#define __WIFI_SOCK_H__
 
typedef struct {
    int     sock;
    char    ip[16];
    uint8_t data[300];
    int     len;
} sock_data_t;
 
typedef void (*sock_recv_callback_t)(sock_data_t);
void wifi_sock_register_callback(sock_recv_callback_t callback_func);

int sock_tcp_write(int sock, uint8_t *data, uint16_t len);

int ap_sock_write(uint8_t *data, uint16_t len);
int sta_sock_write(uint8_t *data, uint16_t len);

int udp_server_write(uint8_t *data, uint16_t len);

int udp_client_write(char *data, uint16_t len);

bool tcp_client_connect_status(void);
 
int tcp_client_write(uint8_t *data, uint16_t len);

void wifi_sock_close(void);

void wifi_sock_init(void);

#endif  /*__WIFI_SOCK_H__ END.*/
