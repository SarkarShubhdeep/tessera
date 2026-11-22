# Tessera firmware: the surface (../ui) on the ESP-Mosaico. Step 03a: screen only.
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/boards/esp_mosaico/usb_hs_console"
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/lvgl")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tessera)
