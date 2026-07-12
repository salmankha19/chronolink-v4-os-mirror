/* platform_stubs.c
   Minimal platform helpers used by virtual drivers for host testing.
   These are deterministic and nonblocking.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h> /* usleep */

int platform_i2c_init(uint8_t bus_id) {
    (void)bus_id;
    /* no-op for host test */
    return 0;
}

int platform_i2c_write(uint8_t bus_id, uint8_t addr, const uint8_t *buf, size_t len) {
    (void)bus_id;
    (void)addr;
    /* For testing, print a short summary */
    printf("[platform_i2c_write] addr=0x%02X len=%zu first=0x%02X\n", addr, len, len ? buf[0] : 0);
    return 0;
}

int platform_i2c_read(uint8_t bus_id, uint8_t addr, uint8_t *buf, size_t len) {
    (void)bus_id;
    (void)addr;
    /* Fill buffer with deterministic test data */
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)(i + 1);
    return 0;
}

void platform_delay_ms(int ms) {
    if (ms <= 0) return;
    usleep((useconds_t)ms * 1000);
}