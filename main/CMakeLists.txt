idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "include"
    REQUIRES boot_manager
    PRIV_REQUIRES core_os
)
