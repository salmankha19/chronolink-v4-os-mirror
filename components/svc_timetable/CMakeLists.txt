idf_component_register(
    SRCS "src/svc_timetable.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        esp_common
        fatfs
        svc_config
        svc_prayer_engine
        svc_time
)
