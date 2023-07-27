/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef __BLE_MESH_H__
#define __BLE_MESH_H__

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_lighting_model_api.h"

#define PROV_OWN_ADDR                   0x0001
#define ROOT_OWN_ADDR                   PROV_OWN_ADDR

#define CID_COMPANY                     0x02E5  // 0x01A8  /**< Alibaba Incorporated */          
#define GENIE_VENDOR_MODEL_ID_SERVER    0x0000  /**< Genie Vendor Model Server ID */
#define GENIE_VENDOR_MODEL_ID_CLIENT    0x0001  /**< Genie Vendor Model Client ID */

#define GENIE_VND_GROUP_ADDR            0xF000  /**< Genie Vendor Receive Group Address */
#define GENIE_OTA_GROUP_ADDR            0xF100  /**< Genie OTA Group Address */
#define GENIE_ALL_GROUP_ADDR            0xCFFF  /**< Genie All Product Group Address */

#define GENIE_LIGHT_GROUP_ADDR          0xC001  /**< Genie Light Product Group Address */
#define GENIE_SWITCH_GROUP_ADDR         0xC002  /**< Genie Switch Product Group Address */
 
// #define ESP_BLE_MESH_MODEL_OP_3(b0, cid)    ((((b0) << 16) | 0xC00000) | (cid))
#define GENIE_MODEL_OP_ATTR_GET         ESP_BLE_MESH_MODEL_OP_3(0xD1,   CID_COMPANY)
#define GENIE_MODEL_OP_ATTR_SET         ESP_BLE_MESH_MODEL_OP_3(0xD2,   CID_COMPANY)
#define GENIE_MODEL_OP_ATTR_SET_UNACK   ESP_BLE_MESH_MODEL_OP_3(0xD3,   CID_COMPANY)
#define GENIE_MODEL_OP_ATTR_STATUS      ESP_BLE_MESH_MODEL_OP_3(0xD4,   CID_COMPANY)
 
#ifndef ADDRSTR
#define ADDRSTR      "%02x%02x%02x%02x%02x%02x"
#define ADDR2STR(a)  (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

/* 大端数据拼接 */
#define BIG_DATA_2_OCTET(data, offset)      (data[offset] << 8 | data[offset + 1])
#define BIG_DATA_4_OCTET(data, offset)      (data[offset] << 24 | data[offset + 1] << 16 | data[offset + 2] << 8 | data[offset + 3])

#define BLE_MESH_STATE_OFF                      0x00
#define BLE_MESH_STATE_ON                       0x01
#define BLE_MESH_STATE_RESTORE                  0x02

/* Following 4 values are as per Mesh Model specification */
#define BLE_MESH_LIGHTNESS_MIN                  0x0001
#define BLE_MESH_LIGHTNESS_MAX                  0xFFFF
#define BLE_MESH_TEMPERATURE_MIN                0x0320      // 800
#define BLE_MESH_TEMPERATURE_MAX                0x4E20      // 20000
#define BLE_MESH_TEMPERATURE_UNKNOWN            0xFFFF

typedef struct {
   uint16_t hue;           // Hue 色度 
   uint16_t saturation;    // Saturation 饱和度
   uint16_t lightness;     // lightness 亮度
} mesh_hsl_t;
 
/*
 * function declaration
 ****************************************************************************************
 */
#define VND_OP_ONOFF    0X01
#define VND_OP_XV       0X02
typedef struct {
   uint8_t opcode;  
   uint8_t len; 
   uint8_t value[6]; 
} mesh_frame_t;  // vendor buffer max lenght = 8bytes

typedef struct {
#define VND_DATA_SIZE  (8) /*!< Rx data max size */  
   uint8_t  addr[6];       /*!< Node device address */
   // uint8_t  dev_uuid[16];  /*!< Device UUID */
   uint16_t unicast_addr;  /*!< Node unicast address */
   uint32_t opcode;        /*!< Rx opcode */
   uint8_t  data[8];       /*!< Rx data */
   uint8_t  len;           /*!< Rx data len */
} mesh_transfer_t;
 
// (receive.opcode, receive.addr, receive.data, receive.len);
typedef void (*ble_mesh_callback_t)(mesh_transfer_t msg);
void ble_mesh_register_callback(ble_mesh_callback_t callback);

void app_ble_mesh_init(void); 

bool ble_mesh_online_status(uint16_t unicast_addr);

bool ble_mesh_onoff_set(uint16_t unicast_addr, bool onoff);
bool ble_mesh_onoff_get(uint16_t unicast_addr);

bool ble_mesh_light_hsl_set(uint16_t dst_addr, esp_ble_mesh_state_change_light_hsl_set_t hsl);
bool ble_mesh_light_hsl_get(uint16_t dst_addr);

bool ble_mesh_light_ctl_set(uint16_t dst_addr, esp_ble_mesh_state_change_light_ctl_set_t ctl);
bool ble_mesh_light_ctl_get(uint16_t dst_addr);

bool ble_mesh_send_vendor_message(uint16_t dst_addr, uint32_t opcode, uint8_t *data, uint8_t len);

uint16_t ble_mesh_provisioner_get_prov_node_addr(uint16_t unicast_addr, uint8_t addr[6]);

esp_err_t ble_mesh_node_reset(uint16_t unicast_addr);

uint16_t ble_mesh_prov_bind_add(uint8_t addr[6], const char *name);
uint16_t ble_mesh_prov_unbind_delete(uint16_t unicast_addr);
void ble_mesh_prov_unbind_delete_all(void);

#endif /* __BLE_MESH_H__ */
