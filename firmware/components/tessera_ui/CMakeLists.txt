# The surface: the same ui/ sources the Mac simulator builds, plus cJSON.
set(root "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB ui_srcs "${root}/ui/*.c" "${root}/ui/fonts/*.c")

idf_component_register(
    SRCS ${ui_srcs} "${root}/third_party/cJSON/cJSON.c"
    INCLUDE_DIRS "${root}/ui" "${root}/third_party/cJSON"
    REQUIRES lvgl)
