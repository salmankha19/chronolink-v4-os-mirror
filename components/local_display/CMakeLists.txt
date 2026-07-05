idf_component_register(
    SRCS "src/local_display.c"
    INCLUDE_DIRS include
    REQUIRES display_api state_manager driver
)
