if(BOOTLOADER_BUILD)
  return()
endif()

idf_component_register(
    SRCS "src/boot_manager.c"
    INCLUDE_DIRS "include" "../core_os/include" "../display_api/include"
  REQUIRES
    esp_common
    nvs_flash
    fatfs
    app_update
    core_os
    event_bus
    display_api
)
