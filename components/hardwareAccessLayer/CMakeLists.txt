# components/hardwareAccessLayer/CMakeLists.txt
idf_component_register(
    SRCS
        src/hal_registry.c
        src/hal_bus_manager.c
        src/hal_gpio.c
        src/hal_health.c
        src/hal_i2c.c
        src/hal_sensor.c
        src/hal_spi.c
    INCLUDE_DIRS "include"
    REQUIRES driver freertos platform_independent_layer
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
