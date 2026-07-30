idf_component_register(
    SRCS "src/chronolink_driver_sdk.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos
)

# Include chronolink_hal headers for type definitions without creating a
# circular build dependency (chronolink_hal REQUIRES chronolink_driver_sdk).
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_SOURCE_DIR}/components/chronolink_hal/include"
)
