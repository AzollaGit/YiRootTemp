# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# 添加外部组件！
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/bluetooth/esp_ble_mesh/common_components/fast_provisioning)
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/bluetooth/esp_ble_mesh/common_components/example_init)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
 
set(PROJECT_VER_NUMBER 2023)
set(PROJECT_VER "01.00.01")
set(PROJECT_NAME "YiRootBaoen")
 
add_definitions(-DCONFIG_PROJECT_NAME="${PROJECT_NAME}")

project(${PROJECT_NAME})

 