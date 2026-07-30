idf_component_register(
    SRCS "src/core_os.c"
    INCLUDE_DIRS "include" "../display_manager/include"
    # Display manager runs in core1_task and is part of Core OS runtime wiring.
    REQUIRES freertos event_bus state_manager display_manager
)
