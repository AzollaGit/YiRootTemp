/* SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "diskio.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_defs.h"
#include "tinyusb.h"
#include "sdmmc_cmd.h"

#include "sdkconfig.h"
#include "hal_usb_msc.h"

// #define TAG  "tusb_msc"

#define CONFIG_USE_INTERNAL_FLASH   1
// #define CONFIG_SDCARD_INTFC_SDIO   1
// #define CONFIG_USE_EXTERNAL_SDCARD  1
// #define CONFIG_SDCARD_INTFC_SPI     1

#define CONFIG_DISK_BLOCK_SIZE      4096

#ifdef CONFIG_USE_EXTERNAL_SDCARD
#ifdef CONFIG_SDCARD_INTFC_SPI
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#define CONFIG_SDCARD_SPI_CS_PIN    41
#define CONFIG_SDCARD_SPI_DI_PIN    40
#define CONFIG_SDCARD_SPI_CLK_PIN   39
#define CONFIG_SDCARD_SPI_DO_PIN    38
#define SDCARD_SPI_CS_PIN        CONFIG_SDCARD_SPI_CS_PIN
#define SPI_MOSI_IO_NUM          CONFIG_SDCARD_SPI_DI_PIN
#define SPI_MISO_IO_NUM          CONFIG_SDCARD_SPI_DO_PIN
#define SPI_SCLK_IO_NUM          CONFIG_SDCARD_SPI_CLK_PIN
#elif defined(CONFIG_SDCARD_INTFC_SDIO) && defined(SOC_SDMMC_HOST_SUPPORTED)
#define SDCARD_SDIO_CLK_PIN      CONFIG_SDCARD_SDIO_CLK_PIN
#define SDCARD_SDIO_CMD_PIN      CONFIG_SDCARD_SDIO_CMD_PIN
#define SDCARD_SDIO_DO_PIN       CONFIG_SDCARD_SDIO_DO_PIN
#define SDCARD_SDIO_DATA_WIDTH   1
#include "driver/sdmmc_host.h"
#else
#error "Not supported interface"
#endif
#endif

// Mount path for the partition
const char *disk_path = MOUNT_POINT;

/* Function to initialize SPIFFS */
static esp_err_t init_fat(const char *base_path)
{
    #ifdef TAG
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    #endif
    esp_err_t ret = ESP_FAIL;
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formated before
#ifdef CONFIG_USE_INTERNAL_FLASH
    // Handle of the wear levelling library instance
    wl_handle_t wl_handle_1 = WL_INVALID_HANDLE;
    #ifdef TAG
    ESP_LOGI(TAG, "using internal flash");
    #endif
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = CONFIG_SPIFFS_MAX_PARTITIONS,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    ret = esp_vfs_fat_spiflash_mount_rw_wl(base_path, "storage", &mount_config, &wl_handle_1);
    if (ret != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
        #endif
        return ESP_FAIL;
    }
#elif defined CONFIG_USE_EXTERNAL_SDCARD
    sdmmc_card_t *card;
    #ifdef TAG
    ESP_LOGI(TAG, "using external sdcard");
    #endif
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = CONFIG_FATFS_FS_LOCK,
        .allocation_unit_size = CONFIG_DISK_BLOCK_SIZE,
    };

#ifdef CONFIG_SDCARD_INTFC_SPI
    #ifdef TAG
    ESP_LOGI(TAG, "Using SPI Interface");
    #endif
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // host.slot = SPI3_HOST;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI_IO_NUM,
        .miso_io_num = SPI_MISO_IO_NUM,
        .sclk_io_num = SPI_SCLK_IO_NUM,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_DISK_BLOCK_SIZE,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        #ifdef TAG
        ESP_LOGE(TAG, "Failed to initialize bus.");
        #endif
        return 1;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDCARD_SPI_CS_PIN;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card);

#elif defined(CONFIG_SDCARD_INTFC_SDIO) && defined(SOC_SDMMC_HOST_SUPPORTED)
    ESP_LOGI(TAG, "Using SDIO Interface");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = SDCARD_SDIO_CLK_PIN;
    slot_config.cmd = SDCARD_SDIO_CMD_PIN;
    slot_config.d0  = SDCARD_SDIO_DO_PIN;
    // To use 1-line SD mode, change this to 1:
    slot_config.width = SDCARD_SDIO_DATA_WIDTH;
    // Enable internal pullup on enabled pins. The internal pullup
    // are insufficient however, please make sure 10k external pullup are
    // connected on the bus. This is for debug / example purpose only.
    // slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);
#else
#error "Not supported interface"
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (ret != ESP_OK) {
        #ifdef TAG
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        #endif
        return ret;
    }
    #ifdef TAG
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    #endif
#endif
    return ESP_OK;
}

#ifdef CONFIG_WIFI_HTTP_ACCESS
extern void app_wifi_main(void);
extern esp_err_t start_file_server(const char *base_path);
#endif
 
void hal_usb_msc_init(void)
{
    /* Initialize file storage */
    ESP_ERROR_CHECK(init_fat(disk_path));
    vTaskDelay(100 / portTICK_PERIOD_MS);
#ifdef CONFIG_WIFI_HTTP_ACCESS
    /* Wi-Fi init with configs from menuconfig */
    app_wifi_main();
    /* Start the file server */
    ESP_ERROR_CHECK(start_file_server(disk_path));
#endif
    esp_log_level_set("tusb_desc", ESP_LOG_WARN);
    #ifdef TAG
    ESP_LOGI(TAG, "USB MSC initialization");
    #endif
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .configuration_descriptor = NULL,
        .external_phy = false,  // In the most cases you need to use a `false` value
        .self_powered = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    #ifdef TAG
    ESP_LOGI(TAG, "USB MSC initialization DONE");
    #endif
}

static uint8_t s_pdrv = 0;
static int s_disk_block_size = 0;
#define LOGICAL_DISK_NUM 1
static bool ejected[LOGICAL_DISK_NUM] = {true};

//--------------------------------------------------------------------+
// tinyusb callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    // Reset the ejection tracking every time we're plugged into USB. This allows for us to battery
    // power the device, eject, unplug and plug it back in to get the drive.
    for (uint8_t i = 0; i < LOGICAL_DISK_NUM; i++) {
        ejected[i] = false;
    }
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    #ifdef TAG
    ESP_LOGW(__func__, "");
    #endif
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allows us to perform remote wakeup
// USB Specs: Within 7ms, device must draw an average current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    #ifdef TAG
    ESP_LOGW(__func__, "");
    #endif
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    #ifdef TAG
    ESP_LOGW(__func__, "");
    #endif
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void tud_msc_write10_complete_cb(uint8_t lun)
{
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return;
    }

    // This write is complete, start the auto reload clock.
    #ifdef TAG
    ESP_LOGD(__func__, "");
    #endif
}

static bool _logical_disk_ejected(void)
{
    bool all_ejected = true;

    for (uint8_t i = 0; i < LOGICAL_DISK_NUM; i++) {
        all_ejected &= ejected[i];
    }

    return all_ejected;
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return;
    }

    const char vid[] = "Yiree";
    const char pid[] = "Mass Storage";
    const char rev[] = "1.0";

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return false;
    }

    if (_logical_disk_ejected()) {
        // Set 0x3a for media not present.
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }

    return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return;
    }

    disk_ioctl(s_pdrv, GET_SECTOR_COUNT, block_count);
    disk_ioctl(s_pdrv, GET_SECTOR_SIZE, block_size);
    s_disk_block_size = *block_size;
    #ifdef TAG
    ESP_LOGI(__func__, "GET_SECTOR_COUNT = %"PRIu32", GET_SECTOR_SIZE = %d", *block_count, *block_size);
    #endif
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return false;
    }

    return true;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    (void) power_condition;

    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return false;
    }

    if (load_eject) {
        if (!start) {
            // Eject but first flush.
            if (disk_ioctl(s_pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            } else {
                ejected[lun] = true;
            }
        } else {
            // We can only load if it hasn't been ejected.
            return !ejected[lun];
        }
    } else {
        if (!start) {
            // Stop the unit but don't eject.
            if (disk_ioctl(s_pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            }
        }

        // Always start the unit, even if ejected. Whether media is present is a separate check.
    }

    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return 0;
    }

    const uint32_t block_count = bufsize / s_disk_block_size;
    disk_read(s_pdrv, buffer, lba, block_count);
    return block_count * s_disk_block_size;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    #ifdef TAG
    ESP_LOGD(__func__, "");
    #endif
    (void) offset;

    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return 0;
    }

    const uint32_t block_count = bufsize / s_disk_block_size;
    disk_write(s_pdrv, buffer, lba, block_count);
    return block_count * s_disk_block_size;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    // read10 & write10 has their own callback and MUST not be handled here
    #ifdef TAG
    ESP_LOGI(__func__, "");
    #endif
    if (lun >= LOGICAL_DISK_NUM) {
        #ifdef TAG
        ESP_LOGE(__func__, "invalid lun number %u", lun);
        #endif
        return 0;
    }

    void const *response = NULL;
    uint16_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        // Host is about to read/write etc ... better not to disconnect disk
        resplen = 0;
        break;

    default:
        // Set Sense = Invalid Command Operation
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

        // negative means error -> tinyusb could stall and/or response with failed status
        resplen = -1;
        break;
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize) {
        resplen = bufsize;
    }

    if (response && (resplen > 0)) {
        if (in_xfer) {
            memcpy(buffer, response, resplen);
        } else {
            // SCSI output
        }
    }

    return resplen;
}
/*********************************************************************** TinyUSB MSC callbacks*/
