#include "hal_gpio_safe.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <stdint.h>

static const char *TAG_HAL_GPIO = "HAL_GPIO";

#ifndef GPIO_PIN_COUNT
#define GPIO_PIN_COUNT 48
#endif

static inline bool pin_is_valid(int pin)
{
    return (pin >= 0 && pin < GPIO_PIN_COUNT);
}

/* Resolve single-bit mask to index, accept small integer indices.
 * Return index >=0 on success, -1 for multi-bit mask or invalid.
 */
static inline int pin_from_mask_or_index(uint64_t v)
{
    if (v == 0) return -1;
    if (v < (uint64_t)GPIO_PIN_COUNT) return (int)v;
    if ((v & (v - 1)) == 0) return __builtin_ctzll(v);
    return -1;
}

typedef void (*pin_cb_t)(int pin, int arg, const char *who);

static void iterate_mask_and_apply(uint64_t mask, pin_cb_t cb, int arg, const char *who)
{
    if (mask == 0) {
        ESP_LOGW(TAG_HAL_GPIO, "%s: empty mask 0x%llx, nothing to do", who ? who : "iterate_mask_and_apply", (unsigned long long)mask);
        return;
    }
    for (int bit = 0; bit < GPIO_PIN_COUNT; ++bit) {
        if (mask & (1ULL << bit)) {
            if (pin_is_valid(bit)) {
                cb(bit, arg, who);
            } else {
                ESP_LOGW(TAG_HAL_GPIO, "%s: mask contains invalid pin %d (mask 0x%llx), skipping", who ? who : "iterate_mask_and_apply", bit, (unsigned long long)mask);
            }
        }
    }
}

static void cb_set_level(int pin, int level, const char *who)
{
    gpio_set_level((gpio_num_t)pin, (uint32_t)level);
}

static void cb_set_direction(int pin, int mode, const char *who)
{
    gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
}

void board_gpio_set_level(int pin, int level, const char *who)
{
    if (pin_is_valid(pin)) {
        gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    } else {
        ESP_LOGW(TAG_HAL_GPIO, "%s: invalid pin %d, skipping gpio_set_level", who ? who : "board_gpio_set_level", pin);
    }
}

void board_gpio_set_level_maskaware(uint64_t pin_or_mask, int level, const char *who)
{
    int idx = pin_from_mask_or_index(pin_or_mask);
    if (idx >= 0) {
        board_gpio_set_level(idx, level, who);
        return;
    }

    /* If pin_or_mask is not a single index, iterate bits and apply */
    iterate_mask_and_apply(pin_or_mask, (pin_cb_t)cb_set_level, level, who);
}

void board_gpio_set_direction(int pin, gpio_mode_t mode, const char *who)
{
    if (pin_is_valid(pin)) {
        gpio_set_direction((gpio_num_t)pin, mode);
    } else {
        ESP_LOGW(TAG_HAL_GPIO, "%s: invalid pin %d, skipping gpio_set_direction", who ? who : "board_gpio_set_direction", pin);
    }
}

void board_gpio_set_direction_maskaware(uint64_t pin_or_mask, gpio_mode_t mode, const char *who)
{
    int idx = pin_from_mask_or_index(pin_or_mask);
    if (idx >= 0) {
        board_gpio_set_direction(idx, mode, who);
        return;
    }

    iterate_mask_and_apply(pin_or_mask, (pin_cb_t)cb_set_direction, (int)mode, who);
}
