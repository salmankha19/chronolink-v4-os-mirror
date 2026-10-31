idf_component_register(
    SRCS
        "src/svc_prayer_engine_service.c"
        "src/adhan_calculator.c"
        "src/jamat_calculator.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        freertos
        log
        event_bus
        svc_time
        svc_config
        svc_prayer_engine
        svc_timetable
)
