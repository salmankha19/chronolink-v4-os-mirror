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
        log
        freertos
)
