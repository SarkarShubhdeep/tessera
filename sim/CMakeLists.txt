cmake_minimum_required(VERSION 3.16)
project(tessera_sim C)

set(CMAKE_C_STANDARD 11)
set(TOP ${CMAKE_CURRENT_SOURCE_DIR}/..)

# LVGL 9.5.0 (the version ESP-Claw uses on the board), configured by ./lv_conf.h
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(${TOP}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

find_package(SDL2 REQUIRED)
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})

# The shared surface (../ui) plus the simulator's main.c
file(GLOB TS_FONTS ${TOP}/ui/fonts/*.c)
add_executable(tessera_sim
    main.c
    ${TOP}/ui/ui.c
    ${TOP}/ui/plates.c
    ${TOP}/ui/setup.c
    ${TS_FONTS}
    ${TOP}/third_party/cJSON/cJSON.c)
target_include_directories(tessera_sim PRIVATE ${TOP} ${TOP}/ui ${TOP}/third_party/cJSON ${SDL2_INCLUDE_DIRS})
target_compile_definitions(tessera_sim PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(tessera_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tessera_sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
