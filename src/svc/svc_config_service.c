/* Service: Config Service placeholder (wraps pil_config). */

#include "pil_config.h"

void svc_config_service_init(void)
{
    pil_config_load();
}
