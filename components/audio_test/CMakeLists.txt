# audio_test.c is only compiled when the Kconfig symbol is set.
# Without this gate, the file compiles but every entry point is
# unused, producing -Wunused-function warnings on every build.

set(AUDIO_TEST_SRCS "")
if(CONFIG_AUDIO_TEST_ENABLE)
    set(AUDIO_TEST_SRCS "src/audio_test.c")
endif()

idf_component_register(
    SRCS ${AUDIO_TEST_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        log
        chronolink_hal
)
