/* main.c - Application main entry point */

/*
 * Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"  
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_proxy_api.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_time_scene_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "settings.h"  /* settings_core_erase() */


#include "ble_mesh.h"
#include "ble_bind.h"

#define TAG "ble_mesh"
 
#define MSG_SEND_TTL        0x02
#define MSG_SEND_REL        false
#define MSG_TIMEOUT         1500
#define MSG_ROLE            ROLE_PROVISIONER

#define COMP_DATA_PAGE_0    0x00

// ble-mesh 心跳包参数说明：https://www.jianshu.com/p/da255b8cdb74
#define HEARTBEAT_GROUP_ADDR    0xC000    // 0x01–0x11	心跳间隔为2的(n-1)次幂秒  
#define HEARTBEAT_PUB_PERIOD    0x05      // Heartbeat messages have a publication period of 2^(3-1) = 4 seconds
#define HEARTBEAT_PUB_TTL       0x02      // Maximum allowed TTL value
#define HEARTBEAT_PUB_COUNT     0xFF      // Heartbeat messages are being sent indefinitely 

#define COMP_DATA_1_OCTET(msg, offset)      (msg[offset])
#define COMP_DATA_2_OCTET(msg, offset)      (msg[offset + 1] << 8 | msg[offset])

//============================================================================================
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static const uint8_t NESH_SEND_COMP_EVT = BIT0;    
static const uint8_t NESH_SEND_TIMEOUT_EVT = BIT1; 
static const uint8_t NESH_PROV_START_EVT = BIT2; 
static const uint8_t NESH_PROV_OPEN_EVT  = BIT3;     
static const uint8_t NESH_PROV_CLOSE_EVT = BIT4;
static const uint8_t NESH_PROV_COMP_EVT  = BIT5;
static const uint8_t NESH_PROV_ERROR_EVT = BIT6;
static const uint8_t NESH_PROV_DELETE_EVT= BIT7;    
 
static EventGroupHandle_t xEvent = NULL;   
static QueueHandle_t xQueue = NULL;   

typedef struct {   
    uint8_t event;          /**< 1:开始配网 ; 2: 正在配网 ；4：配网完成； 0：未配网空闲 */
    mesh_bind_t bind;
} ble_mesh_bind_prov_t;

static ble_mesh_bind_prov_t bind_prov = { 0 }; 

typedef struct {
    uint16_t unicast;
    bool     check;    // 1: 检测到心跳包
    bool     online;   // 1: 在线
} ble_mesh_node_hb_t;
static ble_mesh_node_hb_t node_hb[CONFIG_BLE_MESH_MAX_PROV_NODES] = { 0 };
// 心跳包接收处理
static void ble_mesh_recv_hb(uint16_t hb_src)
{
    /* Judge if the device has been added before */
    for (uint8_t i = 0; i < ARRAY_SIZE(node_hb); i++) {
        if (node_hb[i].unicast == hb_src) {
            node_hb[i].check = true;
            return;
        }
    }

    for (uint8_t j = 0; j < ARRAY_SIZE(node_hb); j++) {
        if (node_hb[j].unicast == ESP_BLE_MESH_ADDR_UNASSIGNED) {
            node_hb[j].unicast = hb_src;
            node_hb[j].check = true;
            return;
        }
    }
}

void ble_mesh_check_heartbeat(TimerHandle_t xTimer)
{
    uint8_t mesh_online = 0;
    for (int i = 0; i < ARRAY_SIZE(node_hb); i++) {
        if (node_hb[i].unicast != BLE_MESH_ADDR_UNASSIGNED){
            if (node_hb[i].check) { 
                node_hb[i].online = true;
                node_hb[i].check = 0; 
                mesh_online++;
            } else {
                node_hb[i].online = false;
            }
        }
    }
    #ifdef TAG
    ESP_LOGW(TAG, "\nble_mesh_heartbeat, online_num = %d\n\n", mesh_online);
    #endif
}

bool ble_mesh_online_status(uint16_t unicast_addr) 
{
    for (int i = 0; i < ARRAY_SIZE(node_hb); i++) {
        if (node_hb[i].unicast != unicast_addr){
            return node_hb[i].online;
        }
    }
    return false;
}

//============================================================================================
 
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = {
    'Y', 'i', DEV_PID, 0x00,  // PID: Yi0000
    0xE5, 0x02, // CID (fixed 16bit)
    0x80, // Bit7: Support OTA;  Bit[0:6]: reserved 
    0x50, // 0x50：BLE5.0; 0x53：BLE5.0 above
    0x12, 0x34,  // ChipId (16bit)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66 // MAC Address  
};

static struct esp_ble_mesh_key_t {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  net_key[16];
    uint8_t  app_key[16];
} prov_key;

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = MSG_SEND_TTL,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};


static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    { GENIE_MODEL_OP_ATTR_SET, GENIE_MODEL_OP_ATTR_STATUS },
    { GENIE_MODEL_OP_ATTR_GET, GENIE_MODEL_OP_ATTR_STATUS },
};

static esp_ble_mesh_client_t hsl_client;
static esp_ble_mesh_client_t ctl_client;
static esp_ble_mesh_client_t onoff_client;
static esp_ble_mesh_client_t config_client;
static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_client_op[] = {
    ESP_BLE_MESH_MODEL_OP(GENIE_MODEL_OP_ATTR_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_CLI(NULL, &hsl_client),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_CLI(NULL, &ctl_client),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_COMPANY, GENIE_VENDOR_MODEL_ID_CLIENT, vnd_client_op, NULL, &vendor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_COMPANY,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .prov_uuid           = dev_uuid,
    .prov_unicast_addr   = PROV_OWN_ADDR,
    .prov_start_address  = 0x0005,
    .prov_attention      = 0x00,
    .prov_algorithm      = 0x00,
    .prov_pub_key_oob    = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags               = 0x00,
    .iv_index            = 0x00,
};

// dst_addr: 可以是单播/组播地址
static void ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common, uint16_t dst_addr, 
                                    esp_ble_mesh_model_t *model, uint32_t opcode)
{
    common->opcode       = opcode;
    common->model        = model;
    common->ctx.net_idx  = ESP_BLE_MESH_NET_PRIMARY; // 这里配网时使用: 0x0000; 通信使用: prov_key.net_idx;
    common->ctx.app_idx  = prov_key.app_idx; // ESP_BLE_MESH_NET_PRIMARY
    common->ctx.addr     = dst_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->ctx.send_rel = MSG_SEND_REL;
    common->msg_timeout  = MSG_TIMEOUT;
    common->msg_role     = MSG_ROLE;
    if (ESP_BLE_MESH_ADDR_IS_GROUP(dst_addr)) { // 组播地址，不设置超时时间！
        common->msg_timeout = 0x0000; 
        common->opcode     += 0x0001;  // 设置为OP_UNACK
    }  
}

static esp_err_t prov_complete(uint16_t node_index, const esp_ble_mesh_octet16_t uuid,
                               uint16_t primary_addr, uint8_t element_num, uint16_t net_idx)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_cfg_client_get_state_t get = { 0 };
    esp_ble_mesh_node_t *node = NULL;
    esp_err_t err;

#ifdef TAG
    ESP_LOGI(TAG, "<prov_complete>, node_index %u, primary_addr 0x%x, element_num %u, net_idx 0x%03x",
                  node_index, primary_addr, element_num, net_idx);
    ESP_LOG_BUFFER_HEX("uuid", uuid, ESP_BLE_MESH_OCTET16_LEN);
#endif

#if 0  // 设置节点名
    char name[10] = {'\0'};
    sprintf(name, "%s%02x", "NODE-", node_index);
    err = esp_ble_mesh_provisioner_set_node_name(node_index, name);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to set node name");
        #endif
        return ESP_FAIL;
    }
#endif

    node = esp_ble_mesh_provisioner_get_node_with_addr(primary_addr);
    if (node == NULL) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to get node 0x%x info", primary_addr);
        #endif
        return ESP_FAIL;
    }

    ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get.comp_data_get.page = COMP_DATA_PAGE_0;
    err = esp_ble_mesh_config_client_get_state(&common, &get);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
        #endif
        return ESP_FAIL;
    }
    bind_prov.bind.uniaddr = node->unicast_addr;  // 记录下单播地址
    xEventGroupSetBits(xEvent, NESH_PROV_COMP_EVT);    // 配网完成！
    return ESP_OK;
}

static void prov_link_open(esp_ble_mesh_prov_bearer_t bearer)
{
    #ifdef TAG
    ESP_LOGI(TAG, "%s link open", bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    #endif
    xEventGroupSetBits(xEvent, NESH_PROV_OPEN_EVT);   // 正在配网 
}
 
static void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason)
{
    #ifdef TAG
    ESP_LOGI(TAG, "%s link close, reason 0x%02x", bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
    #endif
    xEventGroupSetBits(xEvent, NESH_PROV_CLOSE_EVT);   // 正在配网 
}
//==========================================================================================================================
//==========================================================================================================================

// 配网绑定/添加节点
// return: bind_prov.bind.uniaddr 节点的单播地址
uint16_t ble_mesh_prov_bind_add(uint8_t addr[6], const char *name)
{
    memcpy(bind_prov.bind.addr, addr, BD_ADDR_LEN); 
    memset(bind_prov.bind.name, 0, sizeof(bind_prov.bind.name));
    strcpy(bind_prov.bind.name, name);
    bind_prov.bind.uniaddr = 0x0000;
    bind_prov.event = NESH_PROV_START_EVT;  // 开始配网
    xEventGroupClearBits(xEvent, 0xFFFF);
    /* 1：扫描到未配网广播包 */
    bind_prov.event = xEventGroupWaitBits(xEvent, NESH_PROV_OPEN_EVT | NESH_PROV_ERROR_EVT, pdTRUE, pdFALSE, 8000);
    if (bind_prov.event & NESH_PROV_OPEN_EVT) {  
        /* 2：判断是否配网完成（正常：先NESH_PROV_COMP_EVT，再NESH_PROV_CLOSE_EVT） */
        bind_prov.event = xEventGroupWaitBits(xEvent, NESH_PROV_CLOSE_EVT | NESH_PROV_COMP_EVT, pdTRUE, pdFALSE, 8000);
        if ((bind_prov.event & NESH_PROV_COMP_EVT) && bind_prov.bind.uniaddr >= provision.prov_start_address) { 
            if (mesh_bind_add(bind_prov.bind) == true) { // 配网成功！
                bind_prov.event = 0x00;
                return bind_prov.bind.uniaddr;
            }  
        }  
    }  
    bind_prov.event = 0x00;
    return 0x00;
}

// 解绑/删除节点
uint16_t ble_mesh_prov_unbind_delete(uint16_t unicast_addr)
{
    #ifdef TAG
    ESP_LOGI(TAG, "<esp_ble_mesh_node_local_reset>, unicast_addr = 0x%x", unicast_addr);
    #endif
    if (unicast_addr < provision.prov_start_address) return 0x0000;
    ble_mesh_node_reset(unicast_addr);  // 复位节点
    vTaskDelay(400);  // 稍微延时一下
    esp_ble_mesh_provisioner_delete_node_with_addr(unicast_addr);
    xEventGroupWaitBits(xEvent, NESH_PROV_DELETE_EVT, pdTRUE, pdFALSE, 2000);
    mesh_bind_remove(unicast_addr);
    return unicast_addr;
}

void ble_mesh_prov_unbind_delete_all(void)
{
    uint16_t prov_node_num = esp_ble_mesh_provisioner_get_prov_node_count();
    #ifdef TAG
    ESP_LOGI(TAG, "ble_mesh_prov_unbind_delete_all = %d", prov_node_num); 
    #endif   
    if (prov_node_num == 0) return;    
    const esp_ble_mesh_node_t **mesh_node = esp_ble_mesh_provisioner_get_node_table_entry();
    if (*mesh_node == NULL) return;   
    for (uint8_t i = 0; i < prov_node_num; i++) {
        ble_mesh_node_reset(mesh_node[i]->unicast_addr);  // 复位节点
        vTaskDelay(500);  // 稍微延时一下
        esp_ble_mesh_provisioner_delete_node_with_addr(mesh_node[i]->unicast_addr);
        xEventGroupWaitBits(xEvent, NESH_PROV_DELETE_EVT, pdTRUE, pdFALSE, 3000);
    }
    mesh_bind_remove_all();

    settings_core_erase(); // 要在初始化前调用！
}
//=======================================================================================================================
//=======================================================================================================================
static esp_err_t recv_unprov_adv_pkt(uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    /* Due to the API esp_ble_mesh_provisioner_set_dev_uuid_match, Provisioner will only
     * use this callback to report the devices, whose device UUID starts with 'Y' & 'i',
     * to the application layer.
     */
#if 1  // 根据绑定的设备MAC_ADDR进行过滤绑定
    if (bind_prov.event != NESH_PROV_START_EVT) return 0; 
    ESP_LOG_BUFFER_HEX("recv_unprov_adv_pkt->addr", addr, BD_ADDR_LEN);
    ESP_LOG_BUFFER_HEX("bind_prov.bind.addr", bind_prov.bind.addr, BD_ADDR_LEN);
    if (memcmp(bind_prov.bind.addr, addr, BD_ADDR_LEN)) return 0;
    bind_prov.bind.pid = dev_uuid[2];  // pid
#endif 
#ifdef TAG
    ESP_LOG_BUFFER_HEX("Device address", addr, BD_ADDR_LEN);
    ESP_LOGI(TAG, "Address type 0x%02x, adv type 0x%02x", addr_type, adv_type);
    ESP_LOG_BUFFER_HEX("Device UUID", dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    ESP_LOGI(TAG, "oob info 0x%x, bearer %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");
#endif
    esp_ble_mesh_unprov_dev_add_t add_dev = { 0 };
    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = (uint8_t)addr_type;
    memcpy(add_dev.uuid, dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    add_dev.oob_info = oob_info;
    add_dev.bearer = (uint8_t)bearer;
    /* Note: If unprovisioned device adv packets have not been received, we should not add device with ADD_DEV_START_PROV_NOW_FLAG set. */
    esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev, ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (err != ESP_OK) {
        xEventGroupSetBits(xEvent, NESH_PROV_ERROR_EVT);
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to start provisioning device");
        #endif
    }  
    return err;
}

esp_err_t ble_mesh_provisioner_bind_app_key_to_local_model(uint16_t app_key_idx)
{
    esp_err_t err = 0;
    err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, app_key_idx, GENIE_VENDOR_MODEL_ID_CLIENT, CID_COMPANY);
    if (err != ESP_OK) return err;
    err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, app_key_idx, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);
    if (err != ESP_OK) return err;
    err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, app_key_idx, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_CLI, ESP_BLE_MESH_CID_NVAL);
    if (err != ESP_OK) return err;
    err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, app_key_idx, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_CLI, ESP_BLE_MESH_CID_NVAL);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to bind AppKey to vendor client");
        #endif
    }
    return err;
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT, err_code %d", param->provisioner_prov_enable_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT, err_code %d", param->provisioner_prov_disable_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:  /*!< Provisioner receives unprovisioned device beacon event */
        // ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT");
        recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        prov_link_open(param->provisioner_prov_link_open.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        prov_link_close(param->provisioner_prov_link_close.bearer, param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        prov_complete(param->provisioner_prov_complete.node_idx, param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr, param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code %d", param->provisioner_add_unprov_dev_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == 0) {
            const char *name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (name) {
                ESP_LOGI(TAG, "Node %d name %s", param->provisioner_set_node_name_comp.node_index, name);
            }
        }
        #endif
        break; 
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT:        /*!< Provisioner add local network key completion event */
        if (param->provisioner_add_net_key_comp.err_code != 0) {  // error
            ESP_ERROR_CHECK(esp_ble_mesh_provisioner_update_local_net_key(prov_key.net_key, prov_key.net_idx));
        }
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT, net_idx = %d, err_code = %d", param->provisioner_add_net_key_comp.net_idx, param->provisioner_add_net_key_comp.err_code);
        #endif
        break;
    case ESP_BLE_MESH_PROVISIONER_UPDATE_LOCAL_NET_KEY_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_UPDATE_LOCAL_NET_KEY_COMP_EVT, err_code = %d, net_idx = %d", param->provisioner_update_net_key_comp.err_code, param->provisioner_add_net_key_comp.net_idx);
        #endif
        if (param->provisioner_update_net_key_comp.err_code == 0) {
            prov_key.net_idx = param->provisioner_update_net_key_comp.net_idx;
        }
        break;    
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT, err_code %d", param->provisioner_add_app_key_comp.err_code);
        #endif
        if (param->provisioner_add_app_key_comp.err_code == 0) {
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            ble_mesh_provisioner_bind_app_key_to_local_model(prov_key.app_idx);
        } else {  // error! update local app key!
            ESP_ERROR_CHECK(esp_ble_mesh_provisioner_update_local_app_key(prov_key.app_key, prov_key.net_idx, prov_key.app_idx));
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_UPDATE_LOCAL_APP_KEY_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_UPDATE_LOCAL_APP_KEY_COMP_EVT, err_code %d", param->provisioner_update_app_key_comp.err_code);
        #endif
        if (param->provisioner_update_app_key_comp.err_code == 0) {
            prov_key.app_idx = param->provisioner_update_app_key_comp.app_idx;
            ble_mesh_provisioner_bind_app_key_to_local_model(prov_key.app_idx);
        }
        break;
#ifdef TAG        
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code %d", param->provisioner_bind_app_key_to_model_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT, err_code %d", param->provisioner_store_node_comp_data_comp.err_code);
        break;
    case ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT, err_code %d, element_addr 0x%x, company_id 0x%x, model_id 0x%x, group_addr 0x%x",
                 param->model_sub_group_addr_comp.err_code, param->model_sub_group_addr_comp.element_addr,
                 param->model_sub_group_addr_comp.company_id, param->model_sub_group_addr_comp.model_id,
                 param->model_sub_group_addr_comp.group_addr);
        break;
    case ESP_BLE_MESH_PROVISIONER_ENABLE_HEARTBEAT_RECV_COMP_EVT:     /*!< Provisioner start to receive heartbeat message completion event */
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ENABLE_HEARTBEAT_RECV_COMP_EVT");
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_HEARTBEAT_FILTER_TYPE_COMP_EVT: /*!< Provisioner set the heartbeat filter type completion event */
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_HEARTBEAT_FILTER_TYPE_COMP_EVT");
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_HEARTBEAT_FILTER_INFO_COMP_EVT: /*!< Provisioner set the heartbeat filter information completion event */
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_HEARTBEAT_FILTER_INFO_COMP_EVT");
        break;
#endif 
    case ESP_BLE_MESH_PROVISIONER_RECV_HEARTBEAT_MESSAGE_EVT: /*!< Provisioner receive heartbeat message event */
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_HEARTBEAT_MESSAGE_EVT, hb_src 0x%x, 0x%x, ttl: %d, %d, %d, 0x%x, %d", param->provisioner_recv_heartbeat.hb_src,
            param->provisioner_recv_heartbeat.hb_dst, param->provisioner_recv_heartbeat.init_ttl, param->provisioner_recv_heartbeat.rx_ttl,
            param->provisioner_recv_heartbeat.hops, param->provisioner_recv_heartbeat.feature, param->provisioner_recv_heartbeat.rssi);
        #endif
        ble_mesh_recv_hb(param->provisioner_recv_heartbeat.hb_src);
        break;
    case ESP_BLE_MESH_PROVISIONER_DELETE_NODE_WITH_UUID_COMP_EVT:    /*!< Provisioner delete node with uuid completion event */
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_DELETE_NODE_WITH_UUID_COMP_EVT, err_code = %d", param->provisioner_delete_node_with_uuid_comp.err_code);
        ESP_LOG_BUFFER_HEX("del_uuid", param->provisioner_delete_node_with_uuid_comp.uuid, 16);
        #endif
        xEventGroupSetBits(xEvent, NESH_PROV_DELETE_EVT);
        break;
    case ESP_BLE_MESH_PROVISIONER_DELETE_NODE_WITH_ADDR_COMP_EVT:    /*!< Provisioner delete node with unicast address completion event */
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_DELETE_NODE_WITH_ADDR_COMP_EVT, err_code = %d", param->provisioner_delete_node_with_addr_comp.err_code);
        ESP_LOGI(TAG, "del_unicast_addr = %d", param->provisioner_delete_node_with_addr_comp.unicast_addr);
        #endif
        xEventGroupSetBits(xEvent, NESH_PROV_DELETE_EVT);
        break;
    case ESP_BLE_MESH_PROXY_CLIENT_RECV_ADV_PKT_EVT:  /*!< Proxy Client receives Network ID advertising packet event */
        #ifdef TAG 
        ESP_LOGI(TAG, "proxy_client_recv_adv_pkt-> addr="ADDRSTR", rssi = %d, net_idx = %d", 
                       ADDR2STR(param->proxy_client_recv_adv_pkt.addr),
                       param->proxy_client_recv_adv_pkt.rssi,  param->proxy_client_recv_adv_pkt.net_idx);
        #endif
        // esp_ble_mesh_proxy_client_connect(param->proxy_client_recv_adv_pkt.addr, param->proxy_client_recv_adv_pkt.addr_type, param->proxy_client_recv_adv_pkt.net_idx);
        break;
    case ESP_BLE_MESH_PROXY_CLIENT_CONNECTED_EVT:                    /*!< Proxy Client establishes connection successfully event */
    case ESP_BLE_MESH_PROXY_CLIENT_DISCONNECTED_EVT:                 /*!< Proxy Client terminates connection successfully event */
    case ESP_BLE_MESH_PROXY_CLIENT_RECV_FILTER_STATUS_EVT:           /*!< Proxy Client receives Proxy Filter Status event */
    case ESP_BLE_MESH_PROXY_CLIENT_CONNECT_COMP_EVT:                 /*!< Proxy Client connect completion event */
    case ESP_BLE_MESH_PROXY_CLIENT_DISCONNECT_COMP_EVT:              /*!< Proxy Client disconnect completion event */
    case ESP_BLE_MESH_PROXY_CLIENT_SET_FILTER_TYPE_COMP_EVT:         /*!< Proxy Client set filter type completion event */
    case ESP_BLE_MESH_PROXY_CLIENT_ADD_FILTER_ADDR_COMP_EVT:         /*!< Proxy Client add filter address completion event */
    case ESP_BLE_MESH_PROXY_CLIENT_REMOVE_FILTER_ADDR_COMP_EVT:      /*!< Proxy Client remove filter address completion event */    
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROXY_CLIEN_EVT = %d", event);
        #endif
        break;
    default:
        #ifdef TAG 
        ESP_LOGW(TAG, "<ble_mesh_provisioning_cb>, default->event = %d", event);
        #endif
        break;
    }
}

static void ble_mesh_parse_node_comp_data(const uint8_t *data, uint16_t length)
{
#ifdef TAG     
    uint16_t cid, pid, vid, crpl, feat;
    uint16_t loc, model_id, company_id;
    uint8_t nums, numv;
    uint16_t offset;
    int i;

    cid = COMP_DATA_2_OCTET(data, 0);
    pid = COMP_DATA_2_OCTET(data, 2);
    vid = COMP_DATA_2_OCTET(data, 4);
    crpl = COMP_DATA_2_OCTET(data, 6);
    feat = COMP_DATA_2_OCTET(data, 8);
    offset = 10;

    ESP_LOGI(TAG, "********************** Composition Data Start **********************");
    ESP_LOGI(TAG, "* CID 0x%x, PID 0x%x, VID 0x%x, CRPL 0x%x, Features 0x%x *", cid, pid, vid, crpl, feat);
    for (; offset < length; ) {
        loc = COMP_DATA_2_OCTET(data, offset);
        nums = COMP_DATA_1_OCTET(data, offset + 2);
        numv = COMP_DATA_1_OCTET(data, offset + 3);
        offset += 4;
        ESP_LOGI(TAG, "* Loc 0x%x, NumS 0x%02x, NumV 0x%02x *", loc, nums, numv);
        for (i = 0; i < nums; i++) {
            model_id = COMP_DATA_2_OCTET(data, offset);
            ESP_LOGI(TAG, "* SIG Model ID 0x%x *", model_id);
            offset += 2;
        }
        for (i = 0; i < numv; i++) {
            company_id = COMP_DATA_2_OCTET(data, offset);
            model_id = COMP_DATA_2_OCTET(data, offset + 2);
            ESP_LOGI(TAG, "* Vendor Model ID 0x%x, Company ID 0x%x *", model_id, company_id);
            offset += 4;
        }
    }
    ESP_LOGI(TAG, "*********************** Composition Data End ***********************");
#endif    
}


static void ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event, esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_cfg_client_set_state_t set_state = { 0 };
    esp_ble_mesh_node_t *node = NULL;
    esp_err_t err;

    uint32_t opcode = param->params->opcode;
    uint16_t addr = param->params->ctx.addr;

#ifdef TAG 
    ESP_LOGI(TAG, "Config client, err_code %d, event %u, addr 0x%x, opcode 0x%06lx",
        param->error_code, event, addr, opcode);
#endif

    if (param->error_code != ESP_OK) {   // ERROR!!!
        #ifdef TAG
        ESP_LOGW(TAG, "<ble_mesh_config_client_cb>, error_code = %d", param->error_code);
        #endif
        return;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(addr);
    if (!node) {
        #ifdef TAG 
        ESP_LOGE(TAG, "Failed to get node 0x%x info", addr);
        #endif
        return;
    }

#if 1
    switch (opcode) {
    case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET: {
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET data %s", bt_hex(param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len));
        #endif
        ble_mesh_parse_node_comp_data(param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len);

        err = esp_ble_mesh_provisioner_store_node_comp_data(param->params->ctx.addr,
            param->status_cb.comp_data_status.composition_data->data,
            param->status_cb.comp_data_status.composition_data->len);
        if (err != ESP_OK) {
            #ifdef TAG 
            ESP_LOGE(TAG, "Failed to store node composition data");
            #endif
            break;
        }

        ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD);
        set_state.net_key_add.net_idx = prov_key.net_idx;
        memcpy(set_state.net_key_add.net_key, prov_key.net_key, 16);
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err) {
            #ifdef TAG 
            ESP_LOGE(TAG, "%s: Config NetKey Add failed", __func__);
            #endif
            return;
        }
        break;
    }  
    case ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD: {
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD");
        #endif
        ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
        set_state.app_key_add.net_idx = prov_key.net_idx;
        set_state.app_key_add.app_idx = prov_key.app_idx;
        memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err) {
            #ifdef TAG 
            ESP_LOGE(TAG, "%s: Config AppKey Add failed", __func__);
            #endif
            return;
        }
        break;
    }

    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
        #endif
        ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        set_state.model_app_bind.element_addr  = node->unicast_addr;
        set_state.model_app_bind.model_app_idx = prov_key.app_idx;
        set_state.model_app_bind.model_id      = GENIE_VENDOR_MODEL_ID_SERVER;
        set_state.model_app_bind.company_id    = CID_COMPANY;
        // set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
        // set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err != ESP_OK) {
            #ifdef TAG 
            ESP_LOGE(TAG, "%s: Config Model App Bind failed", __func__);
            #endif
            return;
        }
        break;
    }
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
        #endif
        /* After Config Composition Data Status for Config Composition Data Get is received, Config Heartbeat Publication Set will be sent */
        ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET);
        set_state.heartbeat_pub_set.dst     = HEARTBEAT_GROUP_ADDR;
        set_state.heartbeat_pub_set.period  = HEARTBEAT_PUB_PERIOD;     // Heartbeat messages have a publication period of 4 seconds
        set_state.heartbeat_pub_set.ttl     = HEARTBEAT_PUB_TTL;        // Maximum allowed TTL value
        set_state.heartbeat_pub_set.count   = HEARTBEAT_PUB_COUNT;      // Heartbeat messages are being sent indefinitely
        set_state.heartbeat_pub_set.net_idx = prov_key.net_idx;   
        set_state.heartbeat_pub_set.feature = 0x03; // feature
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err != ESP_OK) { 
            #ifdef TAG 
            ESP_LOGE(TAG, "%s: Config Heartbeat Publication Set failed", __func__);
            #endif
        }
        break;
    }
    case ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET: {
        #ifdef TAG 
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
            param->status_cb.heartbeat_pub_status.status, param->status_cb.heartbeat_pub_status.dst, param->status_cb.heartbeat_pub_status.count,
            param->status_cb.heartbeat_pub_status.period, param->status_cb.heartbeat_pub_status.ttl, param->status_cb.heartbeat_pub_status.features,
            param->status_cb.heartbeat_pub_status.net_idx);
        #endif
        break;
    }
    default:
        #ifdef TAG 
        ESP_LOGW(TAG, "<ble_mesh_config_client_cb>, default->event %u", event);
        #endif
        break;
    }
#else 
    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            #ifdef TAG 
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET data %s", bt_hex(param->status_cb.comp_data_status.composition_data->data,
                     param->status_cb.comp_data_status.composition_data->len));
            #endif
            ble_mesh_parse_node_comp_data(param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len);

            err = esp_ble_mesh_provisioner_store_node_comp_data(param->params->ctx.addr,
                param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            if (err != ESP_OK) {
                #ifdef TAG 
                ESP_LOGE(TAG, "Failed to store node composition data");
                #endif
                break;
            }

            ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD);
            set_state.net_key_add.net_idx = prov_key.net_idx;
            memcpy(set_state.net_key_add.net_key, prov_key.net_key, 16);
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err) {
                #ifdef TAG 
                ESP_LOGE(TAG, "%s: Config NetKey Add failed", __func__);
                #endif
                return;
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD: {
            #ifdef TAG 
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD");
            #endif
            ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set_state.app_key_add.net_idx = prov_key.net_idx;
            set_state.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err) {
                #ifdef TAG 
                ESP_LOGE(TAG, "%s: Config AppKey Add failed", __func__);
                #endif
                return;
            }
            break;
        }

        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
            #ifdef TAG 
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            #endif
            ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set_state.model_app_bind.element_addr  = node->unicast_addr;
            set_state.model_app_bind.model_app_idx = prov_key.app_idx;
            set_state.model_app_bind.model_id      = GENIE_VENDOR_MODEL_ID_SERVER;
            set_state.model_app_bind.company_id    = CID_COMPANY;
            // set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
            // set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err != ESP_OK) {
                #ifdef TAG 
                ESP_LOGE(TAG, "%s: Config Model App Bind failed", __func__);
                #endif
                return;
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
            #ifdef TAG 
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            #endif
            /* After Config Composition Data Status for Config Composition Data Get is received, Config Heartbeat Publication Set will be sent */
            ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET);
            set_state.heartbeat_pub_set.dst     = HEARTBEAT_GROUP_ADDR;
            set_state.heartbeat_pub_set.period  = HEARTBEAT_PUB_PERIOD;     // Heartbeat messages have a publication period of 4 seconds
            set_state.heartbeat_pub_set.ttl     = HEARTBEAT_PUB_TTL;        // Maximum allowed TTL value
            set_state.heartbeat_pub_set.count   = HEARTBEAT_PUB_COUNT;      // Heartbeat messages are being sent indefinitely
            set_state.heartbeat_pub_set.net_idx = prov_key.net_idx;   
            set_state.heartbeat_pub_set.feature = 0x03; // feature
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err != ESP_OK) { 
                #ifdef TAG 
                ESP_LOGE(TAG, "%s: Config Heartbeat Publication Set failed", __func__);
                #endif
                return;
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET: {
            #ifdef TAG 
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
                param->status_cb.heartbeat_pub_status.status, param->status_cb.heartbeat_pub_status.dst, param->status_cb.heartbeat_pub_status.count,
                param->status_cb.heartbeat_pub_status.period, param->status_cb.heartbeat_pub_status.ttl, param->status_cb.heartbeat_pub_status.features,
                param->status_cb.heartbeat_pub_status.net_idx);
            #endif
            break;
        }
        default:
            #ifdef TAG 
            ESP_LOGI(TAG, "<ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT> default = 0x%06lx", opcode);
            #endif
            break;    
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        #ifdef TAG 
        ESP_LOGW(TAG, "ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT, opcode = 0x%06lx", param->params->opcode);
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS) {
            ESP_LOG_BUFFER_HEX("Composition data", param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
        }
        #endif
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        #ifdef TAG 
        ESP_LOGW(TAG, "ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT, opcode = 0x%06lx", param->params->opcode);     
        #endif
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET: {
            if (bind_prov.event == 0x00) return; // 没有在配网中！
            esp_ble_mesh_cfg_client_get_state_t get = { 0 };
            ble_mesh_set_msg_common(&common, node->unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
            get.comp_data_get.page = COMP_DATA_PAGE_0;
            err = esp_ble_mesh_config_client_get_state(&common, &get);
            if (err != ESP_OK) {
                #ifdef TAG 
                ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
                #endif
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            break;
        default:
            #ifdef TAG 
            ESP_LOGW(TAG, "<ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT>, default->opcode: 0x%06lx", param->params->opcode);
            #endif
            break;
        }     
        break;        
    default:
        #ifdef TAG 
        ESP_LOGW(TAG, "<ble_mesh_config_client_cb>, default->event %u", event);
        #endif
        break;
    }
#endif
}

static void ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event, esp_ble_mesh_generic_client_cb_param_t *param)
{
    uint32_t opcode = param->params->opcode;
    uint16_t addr = param->params->ctx.addr;

#ifdef TAG 
    ESP_LOGI(TAG, "<%s>, error_code = 0x%02x, event = 0x%02x, addr: 0x%x, opcode: 0x%06lx",
             __func__, param->error_code, event, addr, opcode);
#endif

    if (param->error_code != ESP_OK) {
        #ifdef TAG
        ESP_LOGW(TAG, "<ble_mesh_generic_client_cb>, error_code = %d", param->error_code);
        #endif
        return;
    } 
 
    static bool node_onoff = false;
    mesh_transfer_t queue = { 0 };  
 
    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET: { 
            node_onoff = param->status_cb.onoff_status.present_onoff;
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff: 0x%02x", node_onoff);
            #endif
            queue.opcode = opcode;
            queue.unicast_addr = addr;
            queue.len = 1;
            queue.data[0] = node_onoff;
            xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT);   
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET: {
            node_onoff = param->status_cb.onoff_status.present_onoff;
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET onoff: 0x%02x", node_onoff);
            #endif
            queue.opcode = opcode;
            queue.unicast_addr = addr;
            queue.len = 1;
            queue.data[0] = node_onoff;
            xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT);   
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS: {
            node_onoff = param->status_cb.onoff_status.present_onoff;
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS onoff: 0x%02x", node_onoff);
            #endif
            queue.opcode = opcode;
            queue.unicast_addr = addr;
            queue.len = 1;
            queue.data[0] = node_onoff;
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT opcode: 0x%06lx", opcode);
        #endif
        /* If failed to receive the responses, these messages will be resend */
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET: {
            xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT);   
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET: {
            xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT);   
            break;
        }
        default:
            break;
        }
        break;
    default:
        #ifdef TAG
        ESP_LOGE(TAG, "Not a generic client status message event");
        #endif
        break;
    }

    if (queue.len) {
        xQueueSend(xQueue, &queue, 0);   
    }
}
//=========================================================================================================================
//========================================================================================================================= 
bool ble_mesh_send_msg_wait(void)
{
    xEventGroupClearBits(xEvent, NESH_SEND_COMP_EVT | NESH_SEND_TIMEOUT_EVT);  
    EventBits_t uxBits = xEventGroupWaitBits(xEvent, NESH_SEND_COMP_EVT | NESH_SEND_TIMEOUT_EVT, pdTRUE, pdFALSE, MSG_TIMEOUT);
    if (uxBits & NESH_SEND_COMP_EVT) {  
        return true;
    }  
    return false;
}
 
// dst_addr: 可以是单播/组播地址
bool ble_mesh_onoff_set(uint16_t dst_addr, bool onoff)
{
    static uint8_t tid = 0;
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_generic_client_set_state_t set_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
    common.ctx.net_idx = prov_key.net_idx;  /*!< 通信使用: prov_key.net_idx */
    set_state.onoff_set.op_en = false;
    set_state.onoff_set.onoff = onoff;
    set_state.onoff_set.tid   = tid++;
    set_state.onoff_set.trans_time = 0; /*!< Time to complete state transition (optional) */
    set_state.onoff_set.delay = 0;      /*!< Indicate message execution delay (C.1) */
    esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: Generic OnOff Set failed", __func__);
        #endif
        return false;
    }
    #ifdef TAG
    ESP_LOGI(TAG, "%s: Generic OnOff Set = %d", __func__, onoff);
    #endif
    if (common.msg_timeout == 0) return true;

    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}

bool ble_mesh_onoff_get(uint16_t unicast_addr)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_generic_client_get_state_t get_state = { 0 };
    ble_mesh_set_msg_common(&common, unicast_addr, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET);
    common.ctx.net_idx = prov_key.net_idx;  /*!< 通信使用: prov_key.net_idx */
    esp_err_t err = esp_ble_mesh_generic_client_get_state(&common, &get_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: Generic OnOff Get failed", __func__);
        #endif
        return false;
    }
    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}

esp_err_t ble_mesh_node_reset(uint16_t unicast_addr)
{
    esp_ble_mesh_cfg_client_set_state_t set_state = { 0 };
    esp_ble_mesh_client_common_param_t common = { 0 };
    ble_mesh_set_msg_common(&common, unicast_addr, config_client.model, ESP_BLE_MESH_MODEL_OP_NODE_RESET);
    common.ctx.net_idx = prov_key.net_idx;  /*!< 通信使用: prov_key.net_idx */
    esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
    return err;
}

// dst_addr: 可以是单播/组播地址
// opcode:   GENIE_MODEL_OP_ATTR_SET  / GENIE_MODEL_OP_ATTR_GET
bool ble_mesh_send_vendor_message(uint16_t dst_addr, uint32_t opcode, uint8_t *data, uint8_t len)
{
    esp_ble_mesh_msg_ctx_t ctx = { 0 };
    ctx.addr     = dst_addr;          /*!< Remote address. */
    ctx.net_idx  = prov_key.net_idx;  /*!< 通信使用: prov_key.net_idx */
    ctx.app_idx  = prov_key.app_idx;  /*!< 通信使用: prov_key.app_idx */ 
    ctx.send_ttl = MSG_SEND_TTL;
    ctx.send_rel = MSG_SEND_REL;
    #ifdef TAG
    ESP_LOGI(TAG, "<esp_ble_mesh_client_model_send_msg> addr: 0x%04d, data: 0x%02x 0x%02x", ctx.addr, data[0], data[1]);
    #endif

    bool timeout_enable = true;
    if (ESP_BLE_MESH_ADDR_IS_GROUP(ctx.addr)) {
        timeout_enable = false;
    }

    esp_err_t err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode, len, data, MSG_TIMEOUT, timeout_enable, MSG_ROLE);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to send vendor message <addr: 0x%04d>", ctx.addr);
        #endif
        return false;
    }

    if (timeout_enable == false) return true;

    err = ble_mesh_send_msg_wait();  // 等待发送消息完成
    if (err == true) {  // 正常会先发送成功
        return ble_mesh_send_msg_wait(); // 再等待一次，看看是否会超时！
    }
    return err;
}

//=========================================================================================================================
//=========================================================================================================================

static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    mesh_transfer_t queue = { 0 };
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {  // 接收到节点响应数据
        #ifdef TAG
        ESP_LOGI(TAG, "Receive operation message opcode: 0x%06lx", param->model_operation.opcode);
        #endif
        // ESP_LOG_BUFFER_HEX("VND_MSG", param->model_operation.msg, param->model_operation.length);
        queue.opcode = param->model_operation.opcode;
        queue.unicast_addr = param->model_operation.ctx->addr;
        queue.len = param->model_operation.length;
        if (queue.len > VND_DATA_SIZE) queue.len = VND_DATA_SIZE;
        memcpy(queue.data, param->model_operation.msg, queue.len);
        xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT);  
        break;
    }
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_SEND_COMP_EVT, err_code = %d", param->model_send_comp.err_code);
        #endif
        if (param->model_send_comp.err_code) {
            xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT); 
        } else {
            xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT);   
        }
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        #ifdef TAG
        ESP_LOGW(TAG, "Client message 0x%lx timeout", param->client_send_timeout.opcode);
        #endif
        // param->client_send_timeout.ctx.addr
        xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT);   
        break;
    case ESP_BLE_MESH_MODEL_PUBLISH_UPDATE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_PUBLISH_UPDATE_EVT...");
        #endif
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT: {  // 主动接收节点数据
        #ifdef TAG
        ESP_LOGI(TAG, "Receive publish message opcode: 0x%lx", param->client_recv_publish_msg.opcode);
        #endif
        queue.opcode = param->client_recv_publish_msg.opcode;
        queue.unicast_addr = param->client_recv_publish_msg.ctx->addr;
        queue.len = param->client_recv_publish_msg.length;
        if (queue.len > VND_DATA_SIZE) queue.len = VND_DATA_SIZE;
        memcpy(queue.data, param->client_recv_publish_msg.msg, queue.len);
        break;
    }
    default:
        #ifdef TAG
        ESP_LOGW(TAG, "[ble_mesh_custom_model_cb], default->event = %d", event);
        #endif
        break;
    }

    if (queue.len > 0) {
        xQueueSend(xQueue, &queue, 0); 
    }
}
//=========================================================================================================================
//=========================================================================================================================
// dst_addr: 可以是单播/组播地址
bool ble_mesh_light_hsl_set(uint16_t dst_addr, esp_ble_mesh_state_change_light_hsl_set_t hsl)
{
    static uint8_t tid = 0;
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_light_client_set_state_t set_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, hsl_client.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET);
    common.ctx.net_idx = prov_key.net_idx;              /*!< 通信使用: prov_key.net_idx */
    set_state.hsl_set.op_en = false;                    /*!< Indicate if optional parameters are included */
    set_state.hsl_set.hsl_lightness = hsl.lightness;    /*!< Target value of light hsl lightness state */
    set_state.hsl_set.hsl_hue = hsl.hue;                /*!< Target value of light hsl hue state */
    set_state.hsl_set.hsl_saturation = hsl.saturation;  /*!< Target value of light hsl saturation state */
    set_state.hsl_set.tid = tid++;                      /*!< Transaction ID */
    set_state.hsl_set.trans_time = 0;                   /*!< Time to complete state transition (optional) */
    set_state.hsl_set.delay = 0;                        /*!< Indicate message execution delay (C.1) */
    esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: LIGHT_HSL_SET failed", __func__);
        #endif
        return false;
    }
    if (common.msg_timeout == 0) return true;

    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}

// dst_addr: 是单播地址
bool ble_mesh_light_hsl_get(uint16_t dst_addr)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_light_client_get_state_t get_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, hsl_client.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET);
    common.ctx.net_idx = prov_key.net_idx;              /*!< 通信使用: prov_key.net_idx */
    esp_err_t err = esp_ble_mesh_light_client_get_state(&common, &get_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: LIGHT_CTL_SET  Set failed", __func__);
        #endif
        return false;
    }

    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}


// dst_addr: 可以是单播/组播地址
bool ble_mesh_light_ctl_set(uint16_t dst_addr, esp_ble_mesh_state_change_light_ctl_set_t ctl)
{
    static uint8_t tid = 0;
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_light_client_set_state_t set_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, ctl_client.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET);
    common.ctx.net_idx = prov_key.net_idx;              /*!< 通信使用: prov_key.net_idx */
    set_state.ctl_set.op_en = false;                    /*!< Indicate if optional parameters are included */
    set_state.ctl_set.ctl_lightness = ctl.lightness;    /*!< Target value of light ctl lightness state */
    set_state.ctl_set.ctl_temperatrue = ctl.temperature;/*!< Target value of light ctl temperature state */
    set_state.ctl_set.ctl_delta_uv = ctl.delta_uv;      /*!< Target value of light ctl delta UV state */
    set_state.ctl_set.tid = tid++;                      /*!< Transaction ID */
    set_state.ctl_set.trans_time = 0;                   /*!< Time to complete state transition (optional) */
    set_state.ctl_set.delay = 0;                        /*!< Indicate message execution delay (C.1) */
    set_state.ctl_set.ctl_temperatrue += BLE_MESH_TEMPERATURE_MIN;
    esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: LIGHT_CTL_SET  Set failed", __func__);
        #endif
        return false;
    }
    if (common.msg_timeout == 0) return true;

    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}
 
// dst_addr: 是单播地址
bool ble_mesh_light_ctl_get(uint16_t dst_addr)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_light_client_get_state_t get_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, ctl_client.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET);
    common.ctx.net_idx = prov_key.net_idx;              /*!< 通信使用: prov_key.net_idx */
    esp_err_t err = esp_ble_mesh_light_client_get_state(&common, &get_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: LIGHT_CTL_SET  Set failed", __func__);
        #endif
        return false;
    }

    return ble_mesh_send_msg_wait();  // 等待发送消息完成
}


// dst_addr: 可以是单播/组播地址
// #define BLE_MESH_TEMPERATURE_MIN                0x0320
// #define BLE_MESH_TEMPERATURE_MAX                0x4E20
bool ble_mesh_light_ctl_temp_range_set(uint16_t dst_addr, uint16_t range_min, uint16_t range_max)
{
    esp_ble_mesh_client_common_param_t common = { 0 };
    esp_ble_mesh_light_client_set_state_t set_state = { 0 };
    ble_mesh_set_msg_common(&common, dst_addr, ctl_client.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET);
    common.ctx.net_idx = prov_key.net_idx;                        /*!< 通信使用: prov_key.net_idx */
    set_state.ctl_temperature_range_set.range_min = range_min;    /*!< Target value of light ctl lightness state */
    set_state.ctl_temperature_range_set.range_max = range_max;    /*!< Target value of light ctl temperature state */
    esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
    if (err != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "%s: LIGHT_CTL_SET  Set failed", __func__);
        #endif
        return false;
    }
    return true;
}


static void ble_mesh_light_client_cb(esp_ble_mesh_light_client_cb_event_t event, esp_ble_mesh_light_client_cb_param_t *param)
{
    mesh_transfer_t queue = { 0 };  

    if (param->error_code != ESP_OK) {
        #ifdef TAG
        ESP_LOGW(TAG, "<ble_mesh_light_client_cb>, error_code = %d", param->error_code);
        #endif
        return;
    } 
    
    switch (event) {
    case ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT, opcode = 0x%lx", param->params->opcode);
        #endif
        xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT); 
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET: {
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET, op_en = %d, HSL: %d %d %d, %d", 
                        param->status_cb.hsl_status.op_en,
                        param->status_cb.hsl_status.hsl_hue,
                        param->status_cb.hsl_status.hsl_saturation,
                        param->status_cb.hsl_status.hsl_lightness,
                        param->status_cb.hsl_status.remain_time);
            #endif
            uint16_t hsl_data[3];
            hsl_data[0] = param->status_cb.hsl_status.hsl_hue;
            hsl_data[1] = param->status_cb.hsl_status.hsl_saturation;
            hsl_data[2] = param->status_cb.hsl_status.hsl_lightness;
            queue.len = 6;
            memcpy(queue.data, hsl_data, queue.len);
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET: {
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET, op_en = %d, lightness: %d/%d; temperature: %d/%d, %d", 
                        param->status_cb.ctl_status.op_en,
                        param->status_cb.ctl_status.present_ctl_lightness,
                        param->status_cb.ctl_status.target_ctl_lightness,
                        param->status_cb.ctl_status.present_ctl_temperature,
                        param->status_cb.ctl_status.target_ctl_temperature,
                        param->status_cb.ctl_status.remain_time);
            #endif
            uint16_t ctl_data[2];
            ctl_data[0] = param->status_cb.ctl_status.present_ctl_lightness;
            ctl_data[1] = param->status_cb.ctl_status.present_ctl_temperature;
            queue.len = 4;
            memcpy(queue.data, ctl_data, queue.len);
            break;
        }
        default:
            break;
        }
        
        break;
    case ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT, opcode = 0x%lx", param->params->opcode);
        #endif
        xEventGroupSetBits(xEvent, NESH_SEND_COMP_EVT); 
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET: {
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET OK...");
            #endif
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET: {
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET OK...");
            #endif
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT:  /* 接收: esp_ble_mesh_server_model_send_msg() */
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS:
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS, op_en = %d, HSL: %d %d %d, %d", 
                        param->status_cb.hsl_status.op_en,
                        param->status_cb.hsl_status.hsl_hue,
                        param->status_cb.hsl_status.hsl_saturation,
                        param->status_cb.hsl_status.hsl_lightness,
                        param->status_cb.hsl_status.remain_time);
            #endif
            queue.len = 6;
            queue.data[0] = param->status_cb.hsl_status.hsl_hue >> 8;
            queue.data[1] = param->status_cb.hsl_status.hsl_hue&0xFF;
            queue.data[2] = param->status_cb.hsl_status.hsl_saturation >> 8;
            queue.data[3] = param->status_cb.hsl_status.hsl_saturation&0xFF;
            queue.data[4] = param->status_cb.hsl_status.hsl_lightness >> 8;
            queue.data[5] = param->status_cb.hsl_status.hsl_lightness&0xFF;
            break;
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_STATUS:
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_STATUS, op_en = %d, lightness: %d/%d; temperature: %d/%d, %d", 
                        param->status_cb.ctl_status.op_en,
                        param->status_cb.ctl_status.present_ctl_lightness,
                        param->status_cb.ctl_status.target_ctl_lightness,
                        param->status_cb.ctl_status.present_ctl_temperature,
                        param->status_cb.ctl_status.target_ctl_temperature,
                        param->status_cb.ctl_status.remain_time);
            #endif
            queue.len = 4;
            queue.data[0] = param->status_cb.ctl_status.present_ctl_lightness >> 8;
            queue.data[1] = param->status_cb.ctl_status.present_ctl_lightness&0xFF;
            queue.data[2] = param->status_cb.ctl_status.present_ctl_temperature >> 8;
            queue.data[3] = param->status_cb.ctl_status.present_ctl_temperature&0xFF;
            break;    
        default:
            #ifdef TAG
            ESP_LOGW(TAG, "ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT,  opcode = 0x%lx, err = %d", param->params->opcode, param->error_code);
            #endif
            break;
        }
        break;
    case ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT:
        #ifdef TAG
        ESP_LOGI(TAG, "ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT, opcode = 0x%lx, err = %d", param->params->opcode, param->error_code);
        #endif
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET: {
            xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT); 
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET: {  
            xEventGroupSetBits(xEvent, NESH_SEND_TIMEOUT_EVT); 
            break;
        }
        default:
            #ifdef TAG
            ESP_LOGI(TAG, "ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT, opcode = 0x%lx, err = %d", param->params->opcode, param->error_code);
            #endif
            break;
        }
        break;
    default:
        break;
    }

    if (queue.len) {
        queue.opcode = param->params->opcode;
        queue.unicast_addr = param->params->ctx.addr;
        xQueueSend(xQueue, &queue, 0);   
    }
}
//=========================================================================================================================
//========================================================================================================================= 
// 根据单播地址，找到BLE地址
uint16_t ble_mesh_provisioner_get_prov_node_addr(uint16_t unicast_addr, uint8_t addr[6])
{
    uint16_t prov_node_num = esp_ble_mesh_provisioner_get_prov_node_count();
    #ifdef TAG
    ESP_LOGI(TAG, "prov_node_num = %d", prov_node_num);    
    #endif
    if (prov_node_num == 0) return 0;    
    const esp_ble_mesh_node_t **mesh_node = esp_ble_mesh_provisioner_get_node_table_entry();
    if (*mesh_node == NULL) return 0;   
    for (uint8_t i = 0; i < prov_node_num; i++) {
        if (mesh_node[i]->unicast_addr == unicast_addr) {
            memcpy(addr, mesh_node[i]->addr, 6);
            #ifdef TAG
            ESP_LOG_BUFFER_HEX("node_addr", mesh_node[i]->addr, 6); 
            ESP_LOGI(TAG, "unicast_addr = %d", mesh_node[i]->unicast_addr); 
            #endif
            return mesh_node[i]->unicast_addr;
        }
        if (unicast_addr == 0 && memcmp(addr, mesh_node[i]->addr, 6) == 0) {
            return mesh_node[i]->unicast_addr;
        }
    }
    return 0;
}


static ble_mesh_callback_t ble_mesh_recv_callback = NULL;

void ble_mesh_register_callback(ble_mesh_callback_t callback)
{
    ble_mesh_recv_callback = callback;
}

void ble_mesh_handeler_task(void *arg)
{
    mesh_transfer_t receive = { 0 };

    while (1) {
  
        if (xQueueReceive(xQueue, &receive, 1000) == pdTRUE) {  // portMAX_DELAY
            ble_mesh_recv_callback(receive);
        }

        mesh_bind_update();  // 更新绑定信息
  
#if 0 // test...
        static uint16_t timecnt = 3;
        if (++timecnt > 20) {
            timecnt = 0;
            uint8_t addr[6] = {0x58,0xcf,0x79,0x13,0x44,0x3e};
            uint16_t unicast_addr = ble_mesh_provisioner_get_prov_node_addr(0, addr);
            if (unicast_addr > 0) {
                ESP_LOGI(TAG, "unicast_addr = 0x%x", unicast_addr);
                static uint8_t msg_data[8] = { 0 };
                msg_data[0]++;
                msg_data[1]++;
                msg_data[2]++;
                msg_data[3]++;
                // ble_mesh_prov_unbind_delete_all();
                // continue;
                #if 1
                // static esp_ble_mesh_state_change_light_hsl_set_t hsl;
                // hsl.hue++;
                // hsl.lightness  = 100;
                // hsl.saturation = 100;
                // ble_mesh_light_hsl_set(unicast_addr, hsl);
                // ble_mesh_light_hsl_get(unicast_addr);
                // vTaskDelay(500);
                static esp_ble_mesh_state_change_light_ctl_set_t ctl = {
                    .lightness = 1,
                    .temperature = BLE_MESH_TEMPERATURE_MIN,
                    .delta_uv = 0,
                };
                ctl.lightness++;
                ctl.temperature++;
                ble_mesh_light_ctl_set(unicast_addr, ctl);

                // ble_mesh_light_ctl_temp_range_set(unicast_addr, 1, 3000);

                // ble_mesh_onoff_set(unicast_addr, msg_data[0]%2);
                // ble_mesh_send_vendor_message(unicast_addr, GENIE_MODEL_OP_ATTR_SET, msg_data, 4);
                // continue;
                vTaskDelay(800);
                static uint8_t motor[5] = {0x6E,0x88,0x34,0x05,0x2f}; // 6E 88 34 05 2f
                if (motor[3]) {
                    motor[3] = 0x00;
                    motor[4] = 0x2a;
                } else {
                    motor[3] = 0x05;
                    motor[4] = 0x2f;
                }
                ble_mesh_send_vendor_message(unicast_addr + 1, GENIE_MODEL_OP_ATTR_SET, motor, 5);
                // ble_mesh_onoff_set(unicast_addr + 1, msg_data[0]%2);
                #else  // 组播测试
                ble_mesh_onoff_set(GENIE_LIGHT_GROUP_ADDR, msg_data[0]%2);
                vTaskDelay(800);
                ble_mesh_send_vendor_message(GENIE_VND_GROUP_ADDR, GENIE_MODEL_OP_ATTR_SET, msg_data, 4);
                ESP_LOGI(TAG, "esp_ble_mesh_model_publish = %d", msg_data[0]%2);
                #endif
            }     
        }
#endif
        
    }
}

 
//===================================================================================================  
#include "wifi_ota.h"

 
static void ble_mesh_init(void)
{
    chip_id_t chipid;
    esp_flash_read_unique_chip_id(NULL, &chipid.u64);
    uint16_t chip_id = (chipid.u16[0] ^ chipid.u16[1]) + (chipid.u16[2] ^ chipid.u16[3]);
    dev_uuid[8] = chip_id >> 8;
    dev_uuid[9] = chip_id&0xFF;
    esp_read_mac(dev_uuid + 10, ESP_MAC_BT);   

    /* app_key[16] = chip_id[2] + mac_addr[6] + chipid[8] */
    memcpy(prov_key.app_key, dev_uuid + 8, 8);
    memcpy(prov_key.app_key + 8, chipid.u8, 8);
    prov_key.app_idx = ESP_BLE_MESH_KEY_PRIMARY;
    /* net_key[16] = chip_id[2] + mac_addr[6] + chipid[8] */
    memcpy(prov_key.net_key, prov_key.app_key, 16);
    prov_key.net_key[0] = prov_key.app_key[1];  // 把chip_id[2] 交换一下！
    prov_key.net_key[1] = prov_key.app_key[0];
    prov_key.net_idx = ESP_BLE_MESH_NET_PRIMARY + 1; // ESP_BLE_MESH_NET_PRIMARY: 是系统预置的！
#ifdef TAG
    // ESP_LOG_BUFFER_HEX("dev_mac", dev_uuid + 10, 6);
    ESP_LOGI(TAG, "Mesh Root: "ADDRSTR"|YiRoot%05d\n\n", ADDR2STR(dev_uuid + 10), chip_id); 
#endif
  
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_light_client_callback(ble_mesh_light_client_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_client_callback(ble_mesh_config_client_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb));
 
    ESP_ERROR_CHECK(esp_ble_mesh_init(&provision, &composition));
    ESP_ERROR_CHECK(esp_ble_mesh_client_model_init(&vnd_models[0]));
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_set_dev_uuid_match(dev_uuid, 2, 0x0, false)); 
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)); // | ESP_BLE_MESH_PROV_GATT
    /* Note: 关于net_idx，配网时应该使用默认:0x0000; 绑定模型和通信时使用prov_key.net_idx */
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_add_local_net_key(prov_key.net_key, prov_key.net_idx));
    vTaskDelay(100);
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key, prov_key.net_idx, prov_key.app_idx));

    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_recv_heartbeat(true));
    // ESP_BLE_MESH_HEARTBEAT_FILTER_ACCEPTLIST 、 ESP_BLE_MESH_HEARTBEAT_FILTER_REJECTLIST
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_set_heartbeat_filter_type(ESP_BLE_MESH_HEARTBEAT_FILTER_REJECTLIST));
    esp_ble_mesh_heartbeat_filter_info_t heartbeat_filter_info = {
        .hb_src = PROV_OWN_ADDR, // BLE_MESH_ADDR_UNASSIGNED,
        .hb_dst = HEARTBEAT_GROUP_ADDR,
    };
    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_set_heartbeat_filter_info(ESP_BLE_MESH_HEARTBEAT_FILTER_ADD, &heartbeat_filter_info));
 
    TimerHandle_t timer = xTimerCreate("mesh_hb", pdMS_TO_TICKS(20 * 1000), true, NULL, ble_mesh_check_heartbeat);
    xTimerStart(timer, portMAX_DELAY);
 
    // ble_mesh_prov_unbind_delete_all();  // 要在初始化前调用！

    return;
}
 

void app_ble_mesh_init(void)
{   
    xEvent = xEventGroupCreate();
    xQueue = xQueueCreate(3, sizeof(mesh_transfer_t));

    mesh_bind_init();
    
    /* Initialize the Bluetooth Mesh Subsystem */
    ble_mesh_init();
 
    xTaskCreatePinnedToCore(ble_mesh_handeler_task, "ble_mesh_handeler", 4 * 1024, NULL, 12, NULL, PRO_CPU_NUM);
}
#if 0
ble-mesh 发布订阅介绍：
    https://www.jianshu.com/p/e4704af76449
ble-mesh 心跳包 seq序列号的问题！W (456913) BLE_MESH: Replay: src 0x0005 dst 0xc000 seq 0x000005
    https://github.com/espressif/esp-idf/issues/8669
ble-mesh 关闭重放攻击检测
    https://blog.csdn.net/huaxiu5/article/details/131221376

    把 esp-idf_v5.0/components/bt/esp_ble_mesh/mesh_core/transport.c  屏蔽函数 bt_mesh_rpl_check()
#endif


