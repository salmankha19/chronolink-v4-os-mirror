idf_component_register(
    SRCS "main.c"
    REQUIRES boot_manager ui_producer font svc_time svc_network svc_config svc_prayer_engine svc_prayer_engine_service svc_timetable svc_http
    PRIV_REQUIRES core_os audio_test
)