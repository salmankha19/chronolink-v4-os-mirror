idf_component_register(
    SRCS "src/svc_time.c"
    INCLUDE_DIRS "include"
    REQUIRES
        event_bus
        hil
        esp_netif
        esp_timer
        nvs_flash
        freertos
        log
)
