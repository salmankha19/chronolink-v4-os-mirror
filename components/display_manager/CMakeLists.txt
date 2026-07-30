idf_component_register(
    SRCS
        "src/display_manager.c"
        "src/json_display_executor.c"
    INCLUDE_DIRS "include"
    REQUIRES chronolink_hal event_bus freertos
)
