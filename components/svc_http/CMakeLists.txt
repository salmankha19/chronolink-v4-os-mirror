idf_component_register(
    SRCS "src/svc_http.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        svc_config
        svc_network
        log
        freertos
)
