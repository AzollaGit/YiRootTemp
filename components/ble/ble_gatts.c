/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"  
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "ble_gatts.h"

// #define TAG "BLE_GATTS"

#define CONFIG_SET_RAW_ADV_DATA     1    // 如果使用BLE_MESH就不能使能GATTS广播！！！

#define PROFILE_APP_NUM         1
#define GATTS_APP_ID            0    // called to register application identifier.
#define SVC_INST_ID             0    // srvc_inst_id – [in] the instance id of the service 

//============================================================================
#define GAP_DEVICE_NAME        "YiRoot" // The Device Name Characteristics in GAP
//============================================================================ 

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static const uint8_t BLE_START_ADV_EVENT = BIT0;   // 开启广播完成
static const uint8_t BLE_STOP_ADV_EVENT  = BIT1;   // 暂停广播完成
static const uint8_t BLE_GAP_CLOSE_EVENT = BIT2;   // 断开连接完成
static const uint8_t BLE_GAP_CONN_EVENT  = BIT3;   // 连接完成事件
static const uint8_t BLE_GATTS_UNREG_EVENT  = BIT4;   // GATTS被注销
static EventGroupHandle_t xEvent = NULL;   
static QueueHandle_t xQueue= NULL;
 
#if CONFIG_SET_RAW_ADV_DATA
static uint8_t adv_data_raw[31] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0x0B,               // Manufacturer Specific Data Length
    0xFF,               // Manufacturer Specific Data Type
    0x59, 0x52,         // CID  YR: 0x5952
    0x42,               // VID: 蓝牙版本，0x40：BLE4.0； 0x42：BLE4.2；0x50：BLE5.0；0x52：BLE5.2  
    0x00,               // FMSK: Bit0  0：未配网； 1：已经配网了； 
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC: 设备MAC地址，设备ID！
    /* Complete Local Name in advertising */
    // 0x0A, 0x09, 'Y', 'i', 'r', 'e', 'e', 'R', 'o', 'o', 't'       // 本地广播名："YireeRoot"
};

 
#if 0
设备类型标志
广播中的 flags 字段：
#define BLE_GAP_ADV_FLAG_LE_LIMITED_DISC_MODE         (0x01)   /**< LE Limited Discoverable Mode. */
#define BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE         (0x02)   /**< LE General Discoverable Mode. */
#define BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED         (0x04)   /**< BR/EDR not supported. */
#define BLE_GAP_ADV_FLAG_LE_BR_EDR_CONTROLLER         (0x08)   /**< Simultaneous LE and BR/EDR, Controller. */
#define BLE_GAP_ADV_FLAG_LE_BR_EDR_HOST               (0x10)   /**< Simultaneous LE and BR/EDR, Host. */
#define BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE   (BLE_GAP_ADV_FLAG_LE_LIMITED_DISC_MODE | BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED)   /**< LE Limited Discoverable Mode, BR/EDR not supported. */
#define BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE   (BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE | BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED)   /**< LE General Discoverable Mode, BR/EDR not supported. */
#endif
 
static uint8_t scan_rsp_data_raw[] = {  
    // /* flags */
    0x02, 0x01, 0x06,  /* only ble */
    /* service uuid */
    0x03, 0x03, 0xFF,0xA0
    /* Tx power level */
    // 0x02, 0x0a, 0xEB
};

static esp_ble_adv_params_t gap_adv_params = {
    .adv_int_min        = 160,    // INT * 0.625ms = 100ms
    .adv_int_max        = 320,    // INT * 0.625ms = 100ms
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
#endif 

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    esp_gatt_if_t  gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static uint16_t profile_handle_table[HRS_IDX_NB];

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst profile_tab[PROFILE_APP_NUM];

static void ble_gatts_queue_send(uint8_t idx, void *value, uint8_t len);
 
// 获取BLE事件组
static bool ble_event_group_wait(const uint8_t event, uint32_t wait_time) 
{
    if (profile_tab[GATTS_APP_ID].gatts_if == ESP_GATT_IF_NONE) return false;
    if (xEvent == NULL) return false;
    EventBits_t uxBits = xEventGroupWaitBits(xEvent, event, pdFALSE, pdFALSE, wait_time);
    if (uxBits & event) {  
        return true;
    }  
    return false;
} 

uint8_t ble_connect_status(void)
{
    return ble_event_group_wait(BLE_GAP_CONN_EVENT, 0);  // true: 连接上了
}

/*
 *  GATTS PROFILE ATTRIBUTES
 ****************************************************************************************
 */

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
// UUID 
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
// properties（属性设置）
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
// static const uint8_t char_prop_write_norsp = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE;
// static const uint8_t char_prop_read_ind = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_INDICATE;
// static const uint8_t char_prop_read     = ESP_GATT_CHAR_PROP_BIT_READ;

static const uint16_t gatts_service_uuid = GATTS_CHAR_UUID_PRIMARY;  // 0XFFA0

///GATTS Service - write characteristic, read 
static const uint16_t gatts_write_uuid = GATTS_CHAR_UUID_WRITE; // 0XFFA1
static const uint8_t  gatts_write_val[GATTS_WRITE_DATA_LEN] = {0x00};

///GATTS Service - Notify characteristic, read&write without response
static const uint16_t gatts_notify_uuid = GATTS_CHAR_UUID_NOTIFY;   // 0XFFA2
static const uint8_t  gatts_notify_val[GATTS_READ_DATA_LEN] = {0xFF, 0XA2};
static const uint8_t  gatts_notify_ccc[2]  = {0x12, 0x34};

//================================================================================================
 
#define ESP_ATTR_CONTROL   ESP_GATT_AUTO_RSP    // ESP_GATT_RSP_BY_APP    // 设置写/读操作的响应方式

///Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t gatts_attr_tab[HRS_IDX_NB] =
{
    //GATTS -  Service Declaration
    [HRS_IDX_SVC]  =
    {{ESP_ATTR_CONTROL}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
    sizeof(gatts_service_uuid), sizeof(gatts_service_uuid), (uint8_t *)&gatts_service_uuid}},

    //GATTS -  data write characteristic Declaration
    [IDX_WRITE_CHAR]  =
    {{ESP_ATTR_CONTROL}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

    //GATTS -  data write characteristic Value
    [IDX_WRITE_VAL]  =
    {{ESP_ATTR_CONTROL}, {ESP_UUID_LEN_16, (uint8_t *)&gatts_write_uuid, ESP_GATT_PERM_WRITE,
    GATTS_WRITE_DATA_LEN, sizeof(gatts_write_val), (uint8_t *)gatts_write_val}},

    //GATTS -  data notify characteristic Declaration
    [IDX_NOTIFY_CHAR]  =
    {{ESP_ATTR_CONTROL}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    //GATTS -  data notify characteristic Value
    [IDX_NOTIFY_VAL]   =
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&gatts_notify_uuid, ESP_GATT_PERM_READ,
    GATTS_READ_DATA_LEN, sizeof(gatts_notify_val), (uint8_t *)gatts_notify_val}},

    //GATTS -  data notify characteristic - Client Characteristic Configuration Descriptor
    [IDX_NOTIFY_CFG]   =
    {{ESP_ATTR_CONTROL}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    sizeof(uint16_t), sizeof(gatts_notify_ccc), (uint8_t *)gatts_notify_ccc}},
};

 
static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;
    for (int i = 0; i < HRS_IDX_NB ; i++) {
        if(handle == profile_handle_table[i]) {
            return i;
        }
    }
    return error;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {    
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT = %d", param->adv_data_raw_cmpl.status);
        #endif
        #if CONFIG_SET_RAW_ADV_DATA
        if (param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            esp_ble_gap_start_advertising(&gap_adv_params);
        }
        #endif
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT = %d", param->scan_rsp_data_raw_cmpl.status);
        #endif
        break;    
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
        #endif
        // advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            xEventGroupSetBits(xEvent, BLE_START_ADV_EVENT);   
            xEventGroupClearBits(xEvent, BLE_STOP_ADV_EVENT);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:  /*!< When stop adv complete, the event comes */
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT");
        #endif
        xEventGroupSetBits(xEvent, BLE_STOP_ADV_EVENT);   
        xEventGroupClearBits(xEvent, BLE_START_ADV_EVENT);
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                param->update_conn_params.status,
                param->update_conn_params.min_int,
                param->update_conn_params.max_int,
                param->update_conn_params.conn_int,
                param->update_conn_params.latency,
                param->update_conn_params.timeout);    
        #endif        
        break;
    case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "update phy params status = %d, tx_phy = %d, rx_phy = %d", 
                param->phy_update.status,
                param->phy_update.tx_phy, 
                param->phy_update.rx_phy);
        #endif        
        break;
    default:
        #ifdef TAG
        ESP_LOGI(TAG, "gap_event_handler = %d", event);
        #endif
        break;
    }
}
 
static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_REG_EVT");
        #endif
#if CONFIG_SET_RAW_ADV_DATA 
        esp_ble_gap_set_device_name(GAP_DEVICE_NAME);

        esp_ble_gap_config_adv_data_raw((uint8_t *)adv_data_raw, sizeof(adv_data_raw));

        esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_data_raw, sizeof(scan_rsp_data_raw));
#endif
        esp_ble_gatts_create_attr_tab(gatts_attr_tab, gatts_if, HRS_IDX_NB, SVC_INST_ID);

        break;
    case ESP_GATTS_UNREG_EVT: /*!< When unregister application id, the event comes */
        #ifdef TAG
        ESP_LOGI(TAG, "...ESP_GATTS_UNREG_EVT...");
        #endif
        xEventGroupSetBits(xEvent, BLE_GATTS_UNREG_EVENT); 
        break;
    case ESP_GATTS_READ_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_READ_EVT");
        #endif
        break;
    case ESP_GATTS_WRITE_EVT: {
        #if 0   // debug
        ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT, write value:");
        esp_log_buffer_hex(TAG, param->write.value, param->write.len);
        printf("write.len = %d\n", param->write.len);
        printf("write.value = %s\n", param->write.value);
        #endif 

        uint8_t idx = find_char_and_desr_index(param->write.handle);
        if (idx == IDX_WRITE_VAL) {
            ble_gatts_queue_send(idx, param->write.value, param->write.len); // 发送队列消息给任务
        } else {
            // TODO:
            #ifdef TAG
            uint16_t descr_value = (param->write.value[1] << 8) | param->write.value[0];
            if (descr_value == 0x0001 && param->write.len == 2) {
                ESP_LOGI(TAG, "notify enable");
            } if (descr_value == 0x0002 && param->write.len == 2) {
                ESP_LOGI(TAG, "indicate enable");
            } else if (descr_value == 0x0000) {
                ESP_LOGI(TAG, "notify/indicate disable");
            } 
            #endif
        }

        /* send response when param->write.need_rsp is true*/
        if (param->write.need_rsp){
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_EXEC_WRITE_EVT");
        #endif
        break;
    case ESP_GATTS_MTU_EVT: 
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_MTU_EVT = %dByte", param->mtu.mtu);
        #endif
        xEventGroupSetBits(xEvent, BLE_GAP_CONN_EVENT);   // 设置MTU成功才算真正的连接成功！
        break;

    case ESP_GATTS_CONNECT_EVT:
        profile_tab[GATTS_APP_ID].conn_id  = param->connect.conn_id;
        profile_tab[GATTS_APP_ID].gatts_if = gatts_if;
        xEventGroupClearBits(xEvent, BLE_GAP_CLOSE_EVENT);
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT");
        ESP_LOGI(TAG, "curr connection params  conn_int = %d,latency = %d, timeout = %d",
            param->connect.conn_params.interval,
            param->connect.conn_params.latency,
            param->connect.conn_params.timeout);
        #endif  
        #if 0  // 更新连接参数
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        conn_params.latency = 10;      // Range: 0x0000 to 0x01F3
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 500;     // timeout = 500*10ms = 5000ms
        // start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);

        ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                param->connect.conn_id,
                param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        #endif
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        #endif
#if CONFIG_SET_RAW_ADV_DATA         
        /*!< Connection terminated by local host */
        if (param->disconnect.reason != ESP_GATT_CONN_TERMINATE_LOCAL_HOST) { 
            /* start advertising again when missing the connect */
            esp_ble_gap_start_advertising(&gap_adv_params);
        }
#endif        
        xEventGroupSetBits(xEvent, BLE_GAP_CLOSE_EVENT); 
        xEventGroupClearBits(xEvent, BLE_GAP_CONN_EVENT);
        break;
    case ESP_GATTS_CONF_EVT:
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        break;
    case ESP_GATTS_STOP_EVT:
        break;    
    case ESP_GATTS_OPEN_EVT:
        break;
    case ESP_GATTS_CANCEL_OPEN_EVT:
        break;
    case ESP_GATTS_CLOSE_EVT:
        break;
    case ESP_GATTS_LISTEN_EVT:
        break;
    case ESP_GATTS_CONGEST_EVT:
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
        #ifdef TAG
        ESP_LOGI(TAG, "The number handle = %x", param->add_attr_tab.num_handle);
        #endif
        if (param->create.status == ESP_GATT_OK) {
            if(param->add_attr_tab.num_handle == HRS_IDX_NB) {
                memcpy(profile_handle_table, param->add_attr_tab.handles,
                sizeof(profile_handle_table));
                esp_ble_gatts_start_service(profile_handle_table[HRS_IDX_SVC]);
            } else {
                #ifdef TAG
                ESP_LOGE(TAG, "Create attribute table abnormally, num_handle (%d) doesn't equal to HRS_IDX_NB(%d)",
                        param->add_attr_tab.num_handle, HRS_IDX_NB);
                #endif        
            }
        } else {
            #ifdef TAG
            ESP_LOGE(TAG, "Create attribute table failed, error code = %x", param->create.status);
            #endif
        }
        break;
    }   

    default:
        break;
    }
}


static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            profile_tab[param->reg.app_id].gatts_if = gatts_if;
            profile_tab[param->reg.app_id].app_id   = param->reg.app_id;
        } else {
            #ifdef TAG
            ESP_LOGI(TAG, "Reg app failed, app_id %04x, status %d\n", param->reg.app_id, param->reg.status);
            #endif
            return;
        }
    }

    do {
        for (uint8_t app_id = 0; app_id < PROFILE_APP_NUM; app_id++) {
            if (gatts_if == profile_tab[app_id].gatts_if) {
                if (profile_tab[app_id].gatts_cb) {
                    profile_tab[app_id].gatts_cb(event, gatts_if, param);
                }
                break;
            }
        }
    } while (0);
}

//========================================================================================
//========================================================================================
#if CONFIG_SET_RAW_ADV_DATA
extern uint8_t esp_read_chip_name(const char *dev_name, char *name);
uint8_t app_read_chip_name(char *name)
{
    return esp_read_chip_name(GAP_DEVICE_NAME, name);
}

void ble_config_ble_raw_adv_data(void)
{
    const uint8_t *bt_mac = esp_bt_dev_get_address();  //  蓝牙设备地址（6 个字节），如果蓝牙堆栈未启用，则为 NULL
    if (bt_mac == NULL) {
        return;
    }

    // little_endian_swap_buff(adv_data_raw + 6, bt_mac, 6);  // MAC[6-12]/小端
    memcpy(adv_data_raw + 6, bt_mac, 6);  // MAC[6-12]/大端
 
    char ble_adv_local_name[16];
    uint8_t name_len   = esp_read_chip_name(GAP_DEVICE_NAME, ble_adv_local_name);
    uint8_t name_index = adv_data_raw[0] + 1;     // name索引位置
    adv_data_raw[name_index + 0] = 1 + name_len;  // len
    adv_data_raw[name_index + 1] = 0x09;    // type /* Complete Local Name in advertising */
    memcpy(adv_data_raw + name_index + 2, ble_adv_local_name, name_len);   // name
#ifdef TAG   
    // 0b ff e5 02 50 03 58 cf 79 07 8e bd 0c 09 59 52 2d 52 61 64 61 72 36 30 47
    printf("\n\nble_raw_adv_data: ");
    for (int i = 0; i < name_index; i++) {
        printf("%02x ", adv_data_raw[i]);
    } printf("\r\n\r\n");
#endif    
} 
#endif

// APP写入的数据 即 设备接收到的数据
static void ble_gatts_queue_send(uint8_t idx, void *value, uint8_t len)
{
    if (xQueue == NULL) return;
    ble_transfer_t queue; 
    memcpy(queue.value, value, len);
    queue.idx = idx;
    queue.len = len;
    queue.value[len] = '\0';
    xQueueSend(xQueue, &queue, 0);   // 发送队列消息给任务
}

void ble_gatts_deinit_handle(void)
{
    if (profile_tab[GATTS_APP_ID].gatts_if == ESP_GATT_IF_NONE) return;
    ble_gatts_queue_send(HRS_IDX_NB, "1", 1);
}

// BLE数据对接到APP
void ble_gatts_sendto_app(uint8_t *data, uint16_t len)
{
    if (ble_connect_status() == false) return;
    esp_ble_gatts_send_indicate(profile_tab[GATTS_APP_ID].gatts_if, profile_tab[GATTS_APP_ID].conn_id, profile_handle_table[IDX_NOTIFY_VAL], len, data, false); 
}
 

static ble_gatts_recv_callback_t ble_gatts_recv_cb_func = NULL;
void ble_gatts_register_callback(ble_gatts_recv_callback_t ble_gatts_recv_cb)
{
    ble_gatts_recv_cb_func = ble_gatts_recv_cb;
}

// gatts receive task
void gatts_receive_task(void * arg)
{
    ble_transfer_t queue;
 
    while (true) {
        if (xQueueReceive(xQueue, &queue, portMAX_DELAY)) {  
            // esp_log_buffer_hex("ble receive", queue.value, queue.len);
            if (queue.idx == IDX_WRITE_VAL) {
                ble_gatts_recv_cb_func(queue.idx, queue.value, queue.len);
            } else if (queue.idx == HRS_IDX_NB) {  // 退出！
                break;
            }
        }
    }

    // 注销GATTS任务
    #ifdef TAG
    ESP_LOGI(TAG, "ble_gatts_close_task...");
    ESP_LOGI(TAG, "Free heap, current: %ld, minimum: %ld",
    esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    #endif

    if (ble_connect_status() == true) {
        esp_ble_gatts_close(profile_tab[GATTS_APP_ID].gatts_if, profile_tab[GATTS_APP_ID].conn_id);
        ble_event_group_wait(BLE_GAP_CLOSE_EVENT, 800);  
    } else {
        esp_ble_gap_stop_advertising();  
        ble_event_group_wait(BLE_STOP_ADV_EVENT, 800);  
    }
 
    esp_ble_gatts_app_unregister(profile_tab[GATTS_APP_ID].gatts_if);
    ble_event_group_wait(BLE_GATTS_UNREG_EVENT, 800);  // 等待注销成功

    profile_tab[GATTS_APP_ID].gatts_if = ESP_GATT_IF_NONE;
 
    vEventGroupDelete(xEvent);
  
    vQueueDelete(xQueue);
 
    vTaskDelete(NULL);
}

void bluedroid_stack_deinit(void)
{
    #ifdef TAG
    ESP_LOGI(TAG, "Stop bluetooth...");
    #endif
    // vTaskDelay(200); // BT_APPL: bta_dm_disable BTA_DISABLE_DELAY set to 200 ms
    ESP_ERROR_CHECK( esp_bluedroid_disable() );
    ESP_ERROR_CHECK( esp_bluedroid_deinit() );
    ESP_ERROR_CHECK( esp_bt_controller_disable() );
    ESP_ERROR_CHECK( esp_bt_controller_deinit() );
    return;
}
 
esp_err_t bluedroid_stack_init(void)
{
    esp_log_level_set("BT_INIT", ESP_LOG_WARN);
    esp_log_level_set("BLE_INIT", ESP_LOG_WARN);
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) );
 
    ESP_ERROR_CHECK( esp_bt_controller_init(&bt_cfg) );
     
    ESP_ERROR_CHECK( esp_bt_controller_enable(ESP_BT_MODE_BLE) );
 
    ESP_ERROR_CHECK( esp_bluedroid_init() );
 
    ESP_ERROR_CHECK( esp_bluedroid_enable() );
 
    ESP_ERROR_CHECK( esp_ble_gatt_set_local_mtu(GATTS_MTU_SIZE) );  
 
    return ESP_OK;
}
 
void app_ble_gatts_init(void)
{
    xEvent = xEventGroupCreate();
    xQueue = xQueueCreate(1, sizeof(ble_transfer_t));
    // ESP_ERROR_CHECK( bluedroid_stack_init() );   // 初始化BLE协议栈

#if CONFIG_SET_RAW_ADV_DATA
    // 在初始化BT协议栈之后，先配置广播包数据！
    ble_config_ble_raw_adv_data();  // 必须先初始化ble adv
#endif

    ESP_ERROR_CHECK( esp_ble_gap_register_callback(gap_event_handler) );
 
    ESP_ERROR_CHECK( esp_ble_gatts_register_callback(gatts_event_handler) );
 
    // 注册GATTS_APP
    for (uint8_t app_id = 0; app_id < PROFILE_APP_NUM; app_id++) {
        profile_tab[app_id].gatts_cb = gatts_profile_event_handler;
        profile_tab[app_id].gatts_if = ESP_GATT_IF_NONE;       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        ESP_ERROR_CHECK( esp_ble_gatts_app_register(app_id) );    
    }
 
    xTaskCreatePinnedToCore(gatts_receive_task, "gatts_receive_task", 3 * 1024, NULL, 8, NULL, APP_CPU_NUM);
}


