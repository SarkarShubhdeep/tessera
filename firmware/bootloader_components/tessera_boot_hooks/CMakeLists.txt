idf_component_register(SRCS "hooks.c" REQUIRES hal esp_rom)
# Make sure the hook is linked even though nothing else references it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
