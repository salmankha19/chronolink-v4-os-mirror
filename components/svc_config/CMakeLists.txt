idf_component_register(
    SRCS
        "src/svc_config.c"
        "src/ramadan_window_validator.c"
        "src/svc_config_ramadan_resolution.c"
        "src/hijri_calendar.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        nvs_flash
        log
)
