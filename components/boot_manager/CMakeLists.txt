idf_component_register(
    SRCS "src/boot_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        chronolink_hal
        core_os
        event_bus
        esp_common
)
