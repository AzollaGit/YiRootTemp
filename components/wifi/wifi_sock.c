/**
 * @file    wifi_tcp.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   using LwIP SNTP module and time functions
 * @version 0.1
 * @date    2022-11-03
 * 
 * @copyright Copyright (c) 2022
 * */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"  
#include "esp_log.h"
 
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "lwip/ip4_addr.h"

 
#include "wifi_init.h"
#include "wifi_nvs.h"
#include "wifi_sock.h"

// #define TAG   "wifi_sock"
 
static sock_recv_callback_t sock_recv_callback = NULL; 

void wifi_sock_register_callback(sock_recv_callback_t callback_func)
{
    sock_recv_callback = callback_func;
}

int sock_close(int sock)
{
    if (sock >= 0) {
        shutdown(sock, 0);
        close(sock);
        #ifdef TAG
        ESP_LOGW(TAG, "sock_close = %d...\n\n", sock);
        #endif
        sock = -1;
    }
    return sock;
}

//==========================================================================
//==========================================================================
//==========================================================================
#if 1  

static char host_ip[16] = { 0 };
static int tcp_client_sock = -1;
 
// @Azolla UDP和TCP的端口换一下，分别换到14513和14514上
#define TCP_PORT    14514  
#define UDP_PORT    14513  

static TaskHandle_t tcpTaskHandle = NULL;

static bool tcp_is_connect = false;
bool tcp_client_connect_status(void)
{
    return tcp_is_connect;
}

int tcp_client_write(uint8_t *data, uint16_t len)
{
    if (tcp_client_connect_status() == false) return -4;
    int err = send(tcp_client_sock, data, len, 0);
    if (err < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Error tcp send: errno %d <%s>", errno, strerror(errno));
        #endif
        sock_close(tcp_client_sock);
        tcp_client_sock = -1; 
        return -1;
    }
    return err;
}

static void tcp_client_task(void *pvParameters)
{
    sock_data_t info;
    
    #ifdef TAG
    ESP_LOGI(TAG, "tcp_client_task ruuning...");
    #endif

    vTaskSuspend(tcpTaskHandle);  // 挂起任务

    while (1) {
 
        if (tcp_client_sock < 0) {
            tcp_is_connect = false;
            tcp_client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (tcp_client_sock < 0) {
                #ifdef TAG
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                #endif
                continue;
            }
 
            struct sockaddr_in dest_addr;
            dest_addr.sin_addr.s_addr = inet_addr(host_ip);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(TCP_PORT);

            #ifdef TAG
            ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, TCP_PORT);
            #endif

            int err = connect(tcp_client_sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
            if (err != 0) {
                #ifdef TAG
                ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
                #endif
                sock_close(tcp_client_sock);
                tcp_client_sock = -1;
                vTaskDelay(1000);  // 可能是服务器没有打开！
                continue;
            }  
            #ifdef TAG
            ESP_LOGI(TAG, "Successfully connected");
            #endif
            tcp_is_connect = true;
        }  

        int len = recv(tcp_client_sock, info.data, sizeof(info.data) - 1, 0);
        if (len < 0) { // Error occurred during receiving
            if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;   // Not an error
            }
            #ifdef TAG
            if (errno == ENOTCONN) { // Socket has been disconnected
                ESP_LOGW(TAG, "[sock=%d]: Connection closed", sock);
            } else {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d <%s>", errno, strerror(errno));
            }
            #endif
            sock_close(tcp_client_sock);
            tcp_client_sock = -1;
        } else if (len > 0){ // Data received
            info.sock = tcp_client_sock;
            info.len  = len;
            info.data[len] = '\0'; // Null-terminate whatever is received and treat it like a string
            #ifdef TAG
            ESP_LOGI(TAG, "TCP Received[%d] : %s | %d", info.sock, info.data, info.len);
            #endif
            if (sock_recv_callback != NULL) {
                sock_recv_callback(info);  // 回调函数
            }
        }

        // vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}
#endif

static int udp_client_sock = -1;

int udp_client_write(char *data, uint16_t len)
{
    if (udp_client_sock < 0) return -0xE1;
    uint32_t ip_addr = wifi_sta_ip_addr();
    if (ip_addr == 0) return -0xE2;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = ip_addr | htonl(0xFF); // 广播地址
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);

    int err = sendto(udp_client_sock, data, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        #ifdef TAG
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        #endif
        sock_close(udp_client_sock);
        udp_client_sock = -1; 
    }
    return err;
}


static void udp_client_task(void *pvParameters)
{
    uint8_t data[32];
    while (1) {

        if (wifi_connect_status(1000) == false) {  // 等待wifi连接成功
            // 暂停TCP任务...
            if (tcp_client_sock > 0) {
                vTaskSuspend(tcpTaskHandle);  // 挂起任务
                sock_close(tcp_client_sock);
                tcp_client_sock = -1;
            }
            if (udp_client_sock > 0) {
                sock_close(udp_client_sock);
                udp_client_sock = -1;
            }
            // ESP_LOGW(TAG, "Wait WIFI connect...");
            continue;
        }  

        if (udp_client_sock == -1) {
            udp_client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (udp_client_sock < 0) {
                #ifdef TAG
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                #endif
                vTaskDelay(600 / portTICK_PERIOD_MS);
                continue;
            } 
            // 设置套接字选项以启用地址重用
            int reuseEnable = 1;
            setsockopt(udp_client_sock, SOL_SOCKET, SO_REUSEADDR, &reuseEnable, sizeof(reuseEnable));

            // Enable broadcasting
            int broadcast_enable = 1;
            if (setsockopt(udp_client_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
                #ifdef TAG
                ESP_LOGE(TAG, "Failed to enable broadcasting");
                #endif
                close(udp_client_sock);
                continue;
            }
        }
 
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(udp_client_sock, data, sizeof(data) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) { // Error occurred during receiving
            #ifdef TAG
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            #endif
            sock_close(udp_client_sock);
            udp_client_sock = -1; 
        } else if (len > 0) { // Data received
            data[len] = '\0'; // Null-terminate whatever is received and treat it like a string
            #ifdef TAG
            ESP_LOGI(TAG, "UDP_Client Received[%d] : %s | %d", info.sock, info.data, info.len);
            #endif

            // {"ticket":"ok"}
            if (strncmp((char *)data, "{\"ticket\":\"ok\"}", 15)) {  // 不是鉴权指令！
                continue;
            }

            // Convert ip address to string
            if (source_addr.ss_family == PF_INET) {
                char ip_addr[16] = { 0 };
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, ip_addr, sizeof(ip_addr) - 1);
                #ifdef TAG
                ESP_LOGI(TAG, "Socket accepted ip address: %s", ip_addr);
                #endif
                memcpy(host_ip, ip_addr, strlen(ip_addr));  // 得到服务器IP地址
                vTaskResume(tcpTaskHandle);   // 恢复任务
            }
        }
    }
    vTaskDelete(NULL);
}

void wifi_sock_close(void)
{
    sock_close(tcp_client_sock);
    tcp_client_sock = -1;
    sock_close(udp_client_sock);
    udp_client_sock = -1; 
}

//==========================================================================
//==========================================================================
//==========================================================================
#if 0  // Enable UDP_Server...
#define PORT  3333  
static int udp_ser_sock = -1;
struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
 
int udp_server_write(uint8_t *data, uint16_t len)
{
    if (udp_ser_sock < 0) return -0xE1;
    int err = sendto(udp_ser_sock, data, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
    if (err < 0) {
        ESP_LOGW(TAG, "Error occurred during sendto: errno <%s>", strerror(errno));
        sock_close(udp_ser_sock);
        udp_ser_sock = -1;
    }
    return err;
}

static void udp_server_task(void *arg)
{
    sock_data_t info;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "udp_server_task is running...");
 
    while (true) {

        if (udp_ser_sock == -1) {
            udp_ser_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (udp_ser_sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                vTaskDelay(600 / portTICK_RATE_MS);
                continue;
            } else {
                struct sockaddr_in server_addr = { 0 }; 
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(PORT);
                server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 设置本机IP, inet_addr(APP_CONFIG_SERVER_IP);
                bind(udp_ser_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
            }
        }

        int len = recvfrom(udp_ser_sock, info.data, sizeof(info.data), 0, (struct sockaddr *)&source_addr, &socklen);
        
        if (len < 0) {  // Error occurred during receiving
            ESP_LOGW(TAG, "recvfrom failed: errno <%s>", strerror(errno));
            sock_close(udp_ser_sock);
            udp_ser_sock = -1;
            continue;
        } else if (len > 0) { // Data received
            info.sock = udp_ser_sock;
            info.len  = len;
            info.data[len] = '\0'; // Null-terminate whatever is received and treat it like a string
            // ESP_LOGI(TAG, "UDP_Server Received[%d] : %s | %d", info.sock, info.data, info.len);
            if (sock_recv_callback != NULL) {
                sock_recv_callback(info);  // 回调函数
            }
        }
 
        #if 1  // 打印是哪个服务器IP发来的数据
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            char ip_addr[16] = { 0 };
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, ip_addr, sizeof(ip_addr) - 1);
            ESP_LOGI(TAG, "Socket accepted ip address: %s", ip_addr);
            // ESP_LOGI(TAG, "Socket accepted ip address: %08x", ((struct sockaddr_in *)&source_addr)->sin_addr.s_addr);
        }
        #endif
    }
    vTaskDelete(NULL);
}
#endif

//===============================================================================================
//===============================================================================================
#if 0  // Enable TCP_Server...

#define TCP_MAX_CONN       5        // 最大连接数
static int client_sock[TCP_MAX_CONN];

int tcp_server_write(int sock, uint8_t *data, uint16_t len)
{
    if (sock < 0) return sock;
    if (wifi_connect_status() == false) return -4;
    int err = send(sock, data, len, 0);
    if (err < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Error tcp send: errno %d <%s>", errno, strerror(errno));
        #endif
        sock_close(tcp_client_sock);
        tcp_client_sock = -1; 
        return -1;
    }
    return err;
}

static void tcp_recv_task(void *pvParameters)
{
    int *pragma = (int *)pvParameters;
    int socket = *pragma;
    #ifdef TAG
    ESP_LOGI(TAG, "client_sock = %d", socket);
    #endif
    sock_data_t info;
    while (true) {
        int len = recv(socket, info.data, sizeof(info.data) - 1, 0);
        if (len <= 0) {
            if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;   // Not an error
            }
            #ifdef TAG
            if (errno == ENOTCONN) { // Socket has been disconnected
                ESP_LOGW(TAG, "[sock=%d]: Connection closed", sock);
            } else {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d <%s>", errno, strerror(errno));
            }
            #endif
            break;  // 错误退出，结束接收任务!
        } else {
            info.sock = socket;
            info.len  = len;
            info.data[len] = '\0'; // Null-terminate whatever is received and treat it like a string
            // ESP_LOGI(TAG, "TCP Received[%d] : %s | %d", info.sock, info.data, info.len);
            if (sock_recv_callback != NULL) {
                sock_recv_callback(info);  // 回调函数
            }
            // 根据接收到的数据进行设备设备，区分出对应的socket
            // if (info.data[0] == '0') {  // 比如;
            //     client_sock[0] = socket;
            // }
        }
    }
    /* colse... */
    sock_close(socket);
    vTaskDelete(NULL);
}

static void tcp_server_task(void *pvParameters)
{
#define PORT                        8848
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
 
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        #ifdef TAG
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        #endif
        goto TCP_EXIT;
    }

#if 0  // 采用非阻塞式接收
    // Marking the socket as non-blocking
    int flags = fcntl(listen_sock, F_GETFL);
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        ESP_LOGI(TAG,"Unable to set socket non blocking");
        goto TCP_EXIT;
    }
    #ifdef
    ESP_LOGI(TAG, "Socket marked as non blocking");
    #endif
#endif

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
 
    // Binding socket to the given address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 将32位整数转换为网络字节序
    // dest_addr_ip4->sin_addr.s_addr = inet_addr("192.168.4.1");
    server_addr.sin_port = htons(PORT);
    int err = bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        #ifdef TAG
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        #endif
        goto TCP_EXIT;
    }
  
    err = listen(listen_sock, TCP_MAX_CONN);
    if (err != 0) {
        #ifdef TAG
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        #endif
        goto TCP_EXIT;
    }

    #ifdef TAG
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);
    #endif

    memset(client_sock, -1, sizeof(client_sock));
 
    while (1) {

        #ifdef TAG
        ESP_LOGI(TAG, "Socket listening...");
        #endif
 
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int accept_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (accept_sock < 0) {
            #ifdef TAG
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            #endif
            continue;
        }

        // Set tcp keepalive option
        setsockopt(accept_sock, SOL_SOCKET,  SO_KEEPALIVE,  &keepAlive,    sizeof(int));
        setsockopt(accept_sock, IPPROTO_TCP, TCP_KEEPIDLE,  &keepIdle,     sizeof(int));
        setsockopt(accept_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(accept_sock, IPPROTO_TCP, TCP_KEEPCNT,   &keepCount,    sizeof(int));

        #if 1
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            char ip_addr[16];
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, ip_addr, sizeof(ip_addr) - 1);
            #ifdef TAG
            ESP_LOGI(TAG, "Socket accepted ip address: %s", ip_addr);
            #endif
        }
        #endif
 
        // 创建接收任务
        char task_name[16];
        sprintf(task_name, "tcp_recv_%d", accept_sock);
        #ifdef TAG
        printf("task_name = %s\n", task_name);
        #endif
        xTaskCreatePinnedToCore(tcp_recv_task, task_name, 3 * 1024, (void *)&accept_sock, 9, NULL, APP_CPU_NUM);
    }
TCP_EXIT: 
    vTaskDelete(NULL);
}
#endif

 
void wifi_sock_init(void)
{
    if (1) {
        // 电机通信用的，接收奥格网关的电机控制指令
        xTaskCreatePinnedToCore(tcp_client_task, "tcp_client", 4 * 1024, NULL, 9, &tcpTaskHandle, APP_CPU_NUM); 
        
        // 查找奥格网关用的，一直广播自己的IP数据
        xTaskCreatePinnedToCore(udp_client_task, "udp_client", 3 * 1024, NULL, 8, NULL, APP_CPU_NUM); 
    } else {
        // APP配网用的，接收APP下发的配网信息
        #if 0
        xTaskCreatePinnedToCore(udp_server_task, "udp_server", 4 * 1024, NULL, 8, NULL, APP_CPU_NUM); 
        #endif
    }
}