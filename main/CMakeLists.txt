idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "include"
    REQUIRES boot_manager ui_producer font
    PRIV_REQUIRES core_os audio_test
)
