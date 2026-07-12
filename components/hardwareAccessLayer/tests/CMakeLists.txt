target_sources(${COMPONENT_LIB} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/test_registry.c
    ${CMAKE_CURRENT_LIST_DIR}/virtual_oled.c
    ${CMAKE_CURRENT_LIST_DIR}/virtual_sht4x.c
    ${CMAKE_CURRENT_LIST_DIR}/platform_stubs.c
)
