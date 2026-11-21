cmake_minimum_required(VERSION 3.16)
project(tessera_sim C)

set(CMAKE_C_STANDARD 11)

# LVGL 9.5.0 (same version ESP-Claw uses on the board), configured by ./lv_conf.h
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

find_package(SDL2 REQUIRED)
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})

add_executable(tessera_sim main.c ui/ui.c)
target_include_directories(tessera_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(tessera_sim PRIVATE lvgl ${SDL2_LIBRARIES})
