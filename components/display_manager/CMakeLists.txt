idf_component_register(
    SRCS "src/display_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos chronolink_hal font_engine
)
