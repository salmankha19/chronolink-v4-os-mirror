idf_component_register(
    SRCS "src/svc_network.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        nvs_flash
        freertos
        log
        event_bus
)