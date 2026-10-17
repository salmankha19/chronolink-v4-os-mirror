idf_component_register(
    SRCS "src/svc_config.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        nvs_flash
        log
)
