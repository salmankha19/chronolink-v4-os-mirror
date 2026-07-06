#ifndef CHRONOLINK_HAL_PDL_COMPAT_H
#define CHRONOLINK_HAL_PDL_COMPAT_H

/* Compatibility shim for legacy includes.
   Map old legacy header names to the new component headers. */

#ifndef PDL_PINS_H
#define PDL_PINS_H
#include "pdl_pins.h"
#endif

#ifndef PDL_CAPABILITIES_H
#define PDL_CAPABILITIES_H
#include "pdl_capabilities.h"
#endif

#ifndef PDL_PARTITIONS_H
#define PDL_PARTITIONS_H
#include "pdl_partitions.h"
#endif

/* Legacy symbol aliases (if older code expects different names) */
/* Example:
   #ifndef LED_PIN
   #define LED_PIN PDL_PIN_LED_STATUS
   #endif
*/

#endif /* CHRONOLINK_HAL_PDL_COMPAT_H */
