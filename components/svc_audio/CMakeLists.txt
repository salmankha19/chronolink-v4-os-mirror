idf_component_register(
    SRCS
        "src/svc_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        esp_system
        esp_timer
        event_bus
        svc_config
        hil
        # chmorgan/esp-libhelix-mp3 comes from idf_component.yml (the ESP
        # component manager adds it to this component's requirements), not
        # from this list -- it is not a components/ directory.
)
