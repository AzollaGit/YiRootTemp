idf_component_register(
    SRCS    "wifi_init.c" 
            "wifi_mqtt.c" 
            "wifi_nvs.c" 
            "wifi_sntp.c" 
            "wifi_sock.c" 
            "wifi_ota.c" 
            "wifi_user.c"
            "mac_utils.c"
    INCLUDE_DIRS "include" 
    REQUIRES "hal_drive spi_flash esp_wifi esp_eth mqtt soc freertos mbedtls json app_update esp_http_client"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")

# idf_build_get_property(project_dir PROJECT_DIR)
# idf_component_register(EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)