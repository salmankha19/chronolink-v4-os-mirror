idf_component_register(
    SRCS "src/ui_producer.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos chronolink_hal display_manager font_engine
)
