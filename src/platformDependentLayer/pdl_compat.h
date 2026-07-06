/* Compatibility aliases: expose PlatformDependentLayer public types while keeping pdl_* aliases */
#ifndef PDL_COMPAT_H
#define PDL_COMPAT_H

#include "platformDependentLayer/pdl_board.h"

/* Public descriptive typedef */
typedef PlatformDependentLayer_BoardInfo pdl_board_info_t;

/* Inline wrappers */
static inline const pdl_board_info_t *pdl_board_get_info(void) { return PlatformDependentLayer_get_board_info(); }
static inline void pdl_board_init(void) { PlatformDependentLayer_init(); }

#endif /* PDL_COMPAT_H */
