idf_component_register(
    SRCS
        "src/scheduler_logic.c"
        "src/svc_scheduler.c"
    INCLUDE_DIRS "include"
    REQUIRES
        svc_time
        svc_prayer_engine_service
        event_bus
        log
        freertos
)
