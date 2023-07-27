
set(COMPONENT_SRCS  "ble_gatts.c" "ble_gattc.c" "ble_mesh.c" "ble_mesh_nvs.c" "ble_bind.c") 

set(COMPONENT_ADD_INCLUDEDIRS ". include")

set(COMPONENT_REQUIRES hal_drive wifi spi_flash bt soc freertos mbedtls json)

register_component()
