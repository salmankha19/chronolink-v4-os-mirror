idf_component_register(
    SRCS "src/protocol_framing.c"
    INCLUDE_DIRS include
    REQUIRES state_manager driver
)