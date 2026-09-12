# components/hil/CMakeLists.txt
#
# Hardware Independent Layer. Every file here is a thin adapter over
# chronolink_hal -- no ESP-IDF driver/* headers, no register access, no
# duplicate Kconfig driver selection. chronolink_hal's own Kconfig is
# the single point where a physical driver is chosen; this component
# just gives services/app code a stable, hardware-agnostic API on top.
idf_component_register(
    SRCS
        "src/gpio_api.c"
        "src/i2c_api.c"
        "src/spi_api.c"
        "src/sensor_api.c"
        "src/display_api.c"
        "src/audio_api.c"
        "src/rtc_api.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        freertos
        log
        chronolink_hal
        font_engine
        font
)
