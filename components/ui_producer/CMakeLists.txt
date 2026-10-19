idf_component_register(
    SRCS "src/ui_producer.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos svc_time display_manager font svc_network
)
