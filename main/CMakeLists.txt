idf_component_register(
    SRCS "main.c"
    REQUIRES boot_manager ui_producer font svc_network
    PRIV_REQUIRES core_os audio_test
)