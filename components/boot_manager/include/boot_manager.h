#pragma once
#include <stdint.h>

#define BOOT_FLAG_SAFE_MODE     (1u << 0)
#define BOOT_FLAG_FACTORY_RESET (1u << 1)
#define BOOT_FLAG_OTA_ALLOWED   (1u << 2)

int boot_manager_init(uint32_t flags);
int boot_manager_start(void);
