idf_component_register(
    SRCS "src/chronolink_driver_sdk.c"
    INCLUDE_DIRS "include"
    REQUIRES chronolink_hal freertos
)
