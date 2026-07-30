# components/chronolink_hal/CMakeLists.txt

# Base source list
set(CHRONOLINK_HAL_SRCS
    "src/hal_init.c"
    "src/hal_gpio_esp32.c"
    "src/hal_gpio_safe.c"
    "src/hal_i2c_esp32.c"
    "src/hal_spi_esp32.c"
    "src/hal_rtc_ds3231m.c"
    "src/hal_display.c"
    "src/hal_gfx.c"
    "src/hal_sensors.c"
    "src/pdl_board_esp32.c"
    "${CMAKE_SOURCE_DIR}/src/platformDependentLayer/pdl_board.c"
)

# Conditionally add display backend sources based on Kconfig
if(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_ST7796S)
    list(APPEND CHRONOLINK_HAL_SRCS "src/hal_display_st7796s.c")
endif()

if(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_MAX7219)
    list(APPEND CHRONOLINK_HAL_SRCS "src/hal_display_max7219.c")
endif()

if(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_REMOTE)
    list(APPEND CHRONOLINK_HAL_SRCS "src/hal_display_remote.c")
endif()

# Append mock only when enabled in menuconfig
if(CONFIG_CHRONOLINK_USE_MOCK_DISPLAY)
    list(APPEND CHRONOLINK_HAL_SRCS "src/hal_display_mock.c")
endif()

idf_component_register(
    SRCS ${CHRONOLINK_HAL_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        esp_system
        freertos
        heap
        log
        chronolink_driver_sdk
)

# Pass Kconfig ST7796S settings as compile defines so the driver can use
# them without including sdkconfig.h directly. The driver uses #ifndef
# guards, so these only apply when the user has NOT overridden them in
# board_pins.h or project headers.
if(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_ST7796S)
    # Compute numeric values to avoid expression strings in defines
    math(EXPR ST7796S_SPI_CLOCK_HZ "${CONFIG_CHRONOLINK_HAL_ST7796S_SPI_CLOCK_MHZ} * 1000000")
    math(EXPR ST7796S_DMA_CHUNK_BYTES "${CONFIG_CHRONOLINK_HAL_ST7796S_DMA_CHUNK_KB} * 1024")

    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ST7796S_SPI_HOST=SPI${CONFIG_CHRONOLINK_HAL_ST7796S_SPI_HOST_NUM}_HOST
        ST7796S_SPI_CLOCK_HZ=${ST7796S_SPI_CLOCK_HZ}
        ST7796S_DMA_CHUNK_BYTES=${ST7796S_DMA_CHUNK_BYTES}
        ST7796S_WIDTH=${CONFIG_CHRONOLINK_HAL_ST7796S_WIDTH}
        ST7796S_HEIGHT=${CONFIG_CHRONOLINK_HAL_ST7796S_HEIGHT}
    )
endif()

# Temporary fallback: ensure driver headers are visible to this component
if(NOT DEFINED IDF_PATH)
    set(IDF_PATH $ENV{IDF_PATH})
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platformDependentLayer"
    "${IDF_PATH}/components/driver/include"
    "${IDF_PATH}/components/esp_driver_gpio/include"
    "${IDF_PATH}/components/esp_driver_spi/include"
)
