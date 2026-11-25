idf_component_register(
    SRCS
        "src/svc_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        esp_system
        event_bus
        svc_config
        hil
        minimp3
)
