idf_component_register(
    SRCS "src/audio_test.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        log
        chronolink_hal
)
