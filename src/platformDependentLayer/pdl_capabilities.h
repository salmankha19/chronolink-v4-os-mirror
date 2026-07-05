#ifndef PDL_CAPABILITIES_H
#define PDL_CAPABILITIES_H

#include <stdbool.h>

/* Query board capabilities at runtime. Implementations live in pdl_board.c */

bool pdl_cap_has_rtc(void);
bool pdl_cap_has_dfplayer(void);
bool pdl_cap_has_sensors(void);

#endif /* PDL_CAPABILITIES_H */
