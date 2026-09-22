#ifndef EVENT_BUS_NETWORK_EVENTS_H
#define EVENT_BUS_NETWORK_EVENTS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event payloads for EVENT_NETWORK_*. All fit inside event_t.data
   (see event_bus.h). Consumers cast evt.data to network_event_payload_u. */

typedef struct {
    char ip_str[16];   /* dotted IPv4, null-terminated; "" if unknown */
} network_connected_payload_t;

typedef struct {
    uint8_t reason;    /* wifi_err_reason_t value, or 0 if unknown */
} network_disconnected_payload_t;

typedef struct {
    int32_t err;       /* esp_err_t or wifi_err_reason_t, widened */
    uint8_t stage;     /* svc_network_stage_t (see svc_network.h) */
} network_error_payload_t;

typedef union {
    network_connected_payload_t    connected;
    network_disconnected_payload_t disconnected;
    network_error_payload_t        error;
    uint8_t raw[32];
} network_event_payload_u;

_Static_assert(sizeof(network_event_payload_u) <= 32,
               "network_event_payload_u exceeds event_t.data size");

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_NETWORK_EVENTS_H */