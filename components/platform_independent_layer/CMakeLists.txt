idf_component_register(
    SRCS "pil_config.c"
    INCLUDE_DIRS "."
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    ${CMAKE_SOURCE_DIR}/components/chronolink_hal/include
)