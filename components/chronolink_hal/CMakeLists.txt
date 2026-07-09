idf_component_register(
    SRCS
        "src/hal_init.c"
        "src/hal_gpio_esp32.c"
        "src/hal_i2c_esp32.c"
        "src/hal_spi_esp32.c"
        "src/hal_rtc_ds3231m.c"
        "src/hal_display_st7796s.c"
        "src/hal_display_max7219.c"
        "src/hal_sensors.c"
        "src/pdl_board_esp32.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        esp_system
)

# Temporary fallback: ensure driver headers are visible to this component
if(NOT DEFINED IDF_PATH)
    set(IDF_PATH $ENV{IDF_PATH})
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${IDF_PATH}/components/driver/include"
    "${IDF_PATH}/components/esp_driver_gpio/include"
    "${IDF_PATH}/components/esp_driver_spi/include"
)