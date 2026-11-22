# Tessera firmware: the surface (../ui) on the ESP-Mosaico. Step 03a: screen only.
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/boards/esp_mosaico/usb_hs_console"
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/lvgl")

# Version shown on the start screen: release + commit.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE TESSERA_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(PROJECT_VER "0.1 · ${TESSERA_COMMIT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tessera)
