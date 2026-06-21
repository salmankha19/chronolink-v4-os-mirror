# Main component CMakeLists - contains the app_main entry point

FILE(GLOB app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ""
    REQUIRES core_os local_display remote_bridge
)
