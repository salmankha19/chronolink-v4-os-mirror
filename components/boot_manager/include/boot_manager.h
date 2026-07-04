#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool boot_manager_init(void);

bool cl_fs_mount();

#ifdef __cplusplus
}
#endif
