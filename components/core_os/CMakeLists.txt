idf_component_register(
    SRCS "src/core_os.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos event_bus state_manager
)
