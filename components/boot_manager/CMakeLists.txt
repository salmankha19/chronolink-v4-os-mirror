idf_component_register(
    SRCS "src/boot_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        chronolink_hal
        core_os
        event_bus
        esp_common
        nvs_flash
        fatfs
        app_update
        esp_partition
        esp_driver_gpio
)