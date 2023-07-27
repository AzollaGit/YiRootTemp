/**
 * @file    wifi_init.c
 * @author  Azolla (1228449928@qq.com)
 * @brief   
 * @version 0.1
 * @date    2023-02-06
 * 
 * @copyright Copyright (c) 2023
 * */

#include <string.h>

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h" 
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"

#if (ESP_IDF_VERSION_MAJOR >= 5)
#include "esp_mac.h"
#include "lwip/ip_addr.h"
#endif

#include "wifi_init.h"
#include "wifi_nvs.h"

// 模块将释放名为 Aoge-XXXX（XXXX为模块 mac 地址后两位）的热点，密码 4008502526  
#define EXAMPLE_ESP_WIFI_AP_SSID    "Aoge-"
#define EXAMPLE_ESP_WIFI_AP_PASS    "4008502526"
#define EXAMPLE_MAX_STA_CONN        2
#define EXAMPLE_IP_ADDR             "192.168.4.1"
#define EXAMPLE_ESP_WIFI_AP_CHANNEL 3
 
#define TAG  "_wifi_init_"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static const uint8_t STA_CONNECTED_BIT  = BIT0;  // 连接上了
static const uint8_t STA_DISCONNECT_BIT = BIT1;  // 断开连接了
static const uint8_t STA_STOP_BIT = BIT2;  // 断开连接了
static const uint8_t AP_CONNECTED_BIT   = BIT4;
static EventGroupHandle_t xEvent = NULL;
 
bool wifi_connect_status(uint32_t wait_time) 
{
    if (xEvent == NULL) return false;
    EventBits_t uxBits = xEventGroupWaitBits(xEvent, STA_CONNECTED_BIT, pdFALSE, pdFALSE, wait_time);
    if (uxBits & STA_CONNECTED_BIT) {  
        return true;
    }  
    return false;
}
 
static esp_ip4_addr_t sta_ip_addr;
uint32_t wifi_sta_ip_addr(void)
{
    return sta_ip_addr.addr;
}
 
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START"); 
            #endif
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_STOP:
            xEventGroupSetBits(xEvent, STA_STOP_BIT);   
            break; 
        case WIFI_EVENT_STA_CONNECTED:
            #if EXAMPLE_WIFI_RSSI_THRESHOLD
            if (EXAMPLE_WIFI_RSSI_THRESHOLD) {
                ESP_LOGI(TAG, "setting rssi threshold as %d\n", EXAMPLE_WIFI_RSSI_THRESHOLD);
                esp_wifi_set_rssi_threshold(EXAMPLE_WIFI_RSSI_THRESHOLD);
            }
            #endif 
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disconn = event_data;
            if (disconn->reason == WIFI_REASON_ROAMING) {
                #ifdef TAG
                ESP_LOGW(TAG, "station roaming, do nothing");
                #endif
            } else {
                esp_wifi_connect();
            }
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED"); 
            #endif
            sta_ip_addr.addr = 0;
            xEventGroupSetBits(xEvent, STA_DISCONNECT_BIT);   
            xEventGroupClearBits(xEvent, STA_CONNECTED_BIT);   
            break;    
        }
        //=================================================================
        case WIFI_EVENT_AP_START:
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_AP_START"); 
            #endif
            // wifi_event_ap_start_handler();
            break;
        case WIFI_EVENT_AP_STOP:
             
            break; 
        case WIFI_EVENT_AP_STACONNECTED:
            xEventGroupSetBits(xEvent, AP_CONNECTED_BIT);  
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");  
            #endif
            break;    
        case WIFI_EVENT_AP_STADISCONNECTED:
            xEventGroupClearBits(xEvent, AP_CONNECTED_BIT);   
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");  
            #endif
            break;           
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            #ifdef TAG
            ESP_LOGI(TAG, "WIFI_EVENT_STA_BEACON_TIMEOUT"); 
            #endif
            break;    
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *) event_data;
        sta_ip_addr.addr = evt->ip_info.ip.addr;
        #ifdef TAG
        ESP_LOGI(TAG, "got ip:" IPSTR,  IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "ip.addr = %08lX", evt->ip_info.ip.addr);
        #endif
        xEventGroupSetBits(xEvent, STA_CONNECTED_BIT);  
        xEventGroupClearBits(xEvent, STA_DISCONNECT_BIT);   
    }

}

#if CONFIG_WIFI_MODE_AP 
void wifi_event_ap_start_handler(void)
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    if (netif) {
        esp_netif_dhcps_stop(netif);
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(114, 114, 114, 114);
        dns.ip.type = IPADDR_TYPE_V4;
        dhcps_offer_t dhcps_dns_value = OFFER_DNS;
        ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_dns_value, sizeof(dhcps_dns_value)));
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
        ESP_LOGI(TAG, "ip_napt_enable.......");
    }
}

void wifi_init_softap(void)
{
    #if 0
    if (strcmp(EXAMPLE_IP_ADDR, "192.168.4.1")) {
        int a, b, c, d;
        sscanf(EXAMPLE_IP_ADDR, "%d.%d.%d.%d", &a, &b, &c, &d);
        tcpip_adapter_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(WIFI_IF_AP));
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(WIFI_IF_AP, &ip_info));
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(WIFI_IF_AP));
    }
    #endif
 
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = { 0 },
            .password = EXAMPLE_ESP_WIFI_AP_PASS,
            .ssid_len = 0,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel  = EXAMPLE_ESP_WIFI_AP_CHANNEL
        }
    };
 
    uint8_t mac_addr[6];
    esp_read_mac(mac_addr, ESP_MAC_WIFI_SOFTAP);   

    wifi_config.ap.ssid_len = sprintf((char *)wifi_config.ap.ssid, "" EXAMPLE_ESP_WIFI_AP_SSID "%02X%02X", mac_addr[4], mac_addr[5]);
 
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    wifi_event_ap_start_handler();

    #ifdef TAG
    ESP_LOGI(TAG, "[wifi_init_softap] SSID:%s password:%s",
             wifi_config.ap.ssid, wifi_config.ap.password);
    #endif             
}
#endif    

static void wifi_init_sta(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = { 0 },
            .password = { 0 },
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
                * However these modes are deprecated and not advisable to be used. Incase your Access point
                * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, password, strlen(password));
 
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    #ifdef TAG
    ESP_LOGI(TAG, "[wifi_init_sta] SSID: %s; password: %s",
             wifi_config.sta.ssid, wifi_config.sta.password);
    #endif         
}

// wifi重连
int wifi_sta_reconnect(const char *ssid, const char *password)
{
    if (wifi_connect_status(0) == true) {
        esp_wifi_disconnect();  // 先断开WIFI连接！
        xEventGroupWaitBits(xEvent, STA_DISCONNECT_BIT, pdTRUE, pdFALSE, 5000);  // 等待断开
    }
    esp_wifi_stop();
    xEventGroupWaitBits(xEvent, STA_STOP_BIT, pdTRUE, pdFALSE, 5000);  // 等待暂停
    wifi_init_sta(ssid, password);
    esp_wifi_start();
    return wifi_connect_status(9000);
}
 
static void wifi_initialise(const char *ssid, const char *pswd)
{
    xEvent = xEventGroupCreate(); 

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default()); 
   
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
 
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);   
#if CONFIG_WIFI_MODE_AP
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif); 
#endif
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) ); 
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) ); 
	ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MIN_MODEM) );  // must call this  

    wifi_init_sta(ssid, pswd);

#if CONFIG_WIFI_MODE_AP 
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) ); 
    wifi_init_softap();
#endif 
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void app_wifi_init(const char *ssid, const char *pswd)
{
    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);

    wifi_initialise(ssid, pswd);
} 