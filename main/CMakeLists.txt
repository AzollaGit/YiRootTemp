
set(COMPONENT_SRCS  "app_main.c" 
                    "app_user.c" 
                )

set(COMPONENT_ADD_INCLUDEDIRS ". include")
 

# set(COMPONENT_EMBED_TXTFILES font/myFont.bin)

set(COMPONENT_REQUIRES hal_drive ble wifi driver)

register_component()

# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'spiffs'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
# spiffs_create_partition_image(spiffs files FLASH_IN_PROJECT)
