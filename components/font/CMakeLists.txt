idf_component_register(
    SRCS
        "src/font_builtin_8x8.c"
        "src/font_ui_sans.c"
    INCLUDE_DIRS "include"
    REQUIRES font_engine
)
