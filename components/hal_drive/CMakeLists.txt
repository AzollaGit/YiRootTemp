set(COMPONENT_SRCS  "hal_nvs.c"
                    "hal_timer.c"
                    "hal_exti.c"
                    "hal_gpio.c"
                    "hal_uart.c"
                    "hal_spiffs.c"
                    "hal_rs485.c"
                    "hal_rgb.c"
                    "hal_voice.c"
                    "hal_usb_msc.c"
                    "hal_usb_cdc.c"
)

set(COMPONENT_ADD_INCLUDEDIRS ".")

# set(EXTRA_COMPONENT_DIRS /mnt/e/esp/esp-iot-solution/components/usb/esp_tinyuf2) 
set(COMPONENT_REQUIRES espressif__esp_tinyusb driver usb esp_timer soc nvs_flash spiffs vfs fatfs freertos efuse mbedtls)

register_component()



