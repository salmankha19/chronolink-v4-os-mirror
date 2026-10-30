idf_component_register(
    SRCS "src/svc_prayer_engine_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        event_bus
        svc_time
        svc_config
        svc_prayer_engine
        svc_timetable
)
