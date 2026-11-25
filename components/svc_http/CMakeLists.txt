idf_component_register(
    SRCS
        "src/svc_http.c"
        "src/index_html.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        svc_config
        svc_network
        svc_prayer_engine_service
        svc_timetable
        svc_time
        # event_bus is unconditional even though only the /debug/audio hook
        # (CHRONOLINK_TEST_HOOKS) publishes an event: the linkage must not
        # depend on a build flag, the handler itself does. Same rule as
        # svc_time above.
        event_bus
        nvs_flash
        app_update
        log
        freertos
)
