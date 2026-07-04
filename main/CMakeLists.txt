idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES boot_manager core_os display_api freertos log
)
