#include "chronolink_driver_sdk.h"

static const chronolink_display_driver_t *s_custom_drv = NULL;

hal_status_t chronolink_driver_register(const chronolink_display_driver_t *drv)
{
    if (drv == NULL) {
        return HAL_ERR_INIT;
    }

    s_custom_drv = drv;
    return HAL_OK;
}

void chronolink_driver_unregister(void)
{
    s_custom_drv = NULL;
}

bool chronolink_driver_is_registered(void)
{
    return s_custom_drv != NULL;
}

const chronolink_display_driver_t *chronolink_driver_get(void)
{
    return s_custom_drv;
}
