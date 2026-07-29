if(BOOTLOADER_BUILD)
  return()
endif()

idf_component_register(
    SRCS "src/boot_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        event_bus
)
